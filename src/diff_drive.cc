#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <angles/angles.h>
#include <jaguar/diff_drive.h>

using can::JaguarBridge;

namespace jaguar {

template <typename T>
inline T sgn(T x)
{
    if      (x > 0) return  1;
    else if (x < 0) return -1;
    else            return  0;
}

DiffDriveRobot::DiffDriveRobot(DiffDriveSettings const &settings)
    : bridge_(settings.port)
    , jag_broadcast_(bridge_)
    , jag_left_(bridge_, settings.id_left)
    , jag_right_(bridge_, settings.id_right)
    , status_ms_(settings.status_ms)
    , velocity_left_(0.0), velocity_right_(0.0)
    , robot_radius_(settings.robot_radius_m)
    , wheel_circum_(2 * M_PI * settings.wheel_radius_m)
    , current_rpm_left_(0), current_rpm_right_(0)
    , target_rpm_left_(0), target_rpm_right_(0)
    , accel_max_(settings.accel_max_mps2)
{
    // This is necessary for the Jaguars to work after a fresh boot, even if
    // we never called system_halt() or system_reset().
    // FIXME: Convert from revolutions to meters using the robot model.
    block(
        jag_left_.config_brake_set(settings.brake),
        jag_right_.config_brake_set(settings.brake)
    );

    speed_init();
    odom_init();
    diag_init();
    jag_broadcast_.system_resume();
}

DiffDriveRobot::~DiffDriveRobot(void)
{
}

void DiffDriveRobot::drive(double v, double omega)
{
    double const v_left  = v - robot_radius_ * omega;
    double const v_right = v + robot_radius_ * omega;
    drive_raw(v_left, v_right);
}

void DiffDriveRobot::drive_raw(double v_left, double v_right)
{
    target_rpm_left_  = v_left  * 60 / wheel_circum_;
    target_rpm_right_ = v_right * 60 / wheel_circum_;
}

void DiffDriveRobot::drive_spin(double dt)
{
    double const residual_rpm_left  = target_rpm_left_  - current_rpm_left_;
    double const residual_rpm_right = target_rpm_right_ - current_rpm_right_;

    // Cap the acceleration at the limiting value.
    double const drpm_max = accel_max_ * dt * 60 / wheel_circum_;

    if (fabs(residual_rpm_left) <= drpm_max) {
        current_rpm_left_ = target_rpm_left_;
    } else {
        current_rpm_left_ += sgn(residual_rpm_left) * drpm_max;
    }

    if (fabs(residual_rpm_right) <= drpm_max) {
        current_rpm_right_ = target_rpm_right_;
    } else {
        current_rpm_right_ += sgn(residual_rpm_right) * drpm_max;
    }

    block(
        jag_left_.speed_set(current_rpm_left_),
        jag_right_.speed_set(current_rpm_right_)
    );
}

void DiffDriveRobot::drive_brake(bool braking)
{
    jaguar::BrakeCoastSetting::Enum value;
    if (braking) {
        value = jaguar::BrakeCoastSetting::kOverrideBrake;
    } else {
        value = jaguar::BrakeCoastSetting::kOverrideCoast;
    }

    block(
        jag_left_.config_brake_set(value),
        jag_right_.config_brake_set(value)
    );
}

void DiffDriveRobot::heartbeat(void)
{
    jag_broadcast_.heartbeat();
}

/*
 * Wheel Odometry
 */
void DiffDriveRobot::odom_init(void)
{
    odom_curr_left_  = 0;
    odom_curr_right_ = 0;
    odom_last_left_  = 0;
    odom_last_right_ = 0;
    x_ = 0.0;
    y_ = 0.0;
    theta_ = 0.0;

    // Configure the Jaguars to use optical encoders. They are used as both a
    // speed reference for velocity control and position reference for
    // odometry. As such, they must be configured for position control even
    // though we are are using speed control mode.
    block(
        jag_left_.position_set_reference(PositionReference::kQuadratureEncoder),
        jag_right_.position_set_reference(PositionReference::kQuadratureEncoder)
    );

    block(
        jag_left_.periodic_config_odom(0,
            boost::bind(&DiffDriveRobot::odom_update, this,
                kLeft, boost::ref(odom_last_left_), boost::ref(odom_curr_left_), _1, _2
            )
        ),
        jag_right_.periodic_config_odom(0,
            boost::bind(&DiffDriveRobot::odom_update, this,
                kRight, boost::ref(odom_last_right_), boost::ref(odom_curr_right_), _1, _2
            )
        )
    );

    // TODO: Make this a parameter.
    block(
        jag_left_.periodic_enable(0, 200),
        jag_right_.periodic_enable(0, 200)
    );
}

void DiffDriveRobot::odom_attach(boost::function<OdometryCallback> callback)
{
    odom_signal_.connect(callback);
}

void DiffDriveRobot::odom_update(Side side, double &last_pos, double &curr_pos,
                                 double new_pos, double velocity)
{
    // Keep track of the last two encoder readings to measure the change.
    last_pos = curr_pos;
    curr_pos = new_pos;

    // Update the velocities.
    if (side == kLeft) {
        velocity_left_ = velocity;
    } else if (side == kRight) {
        velocity_right_ = velocity;
    }

    // Update the state variables to indicate which odometry readings we already
    // have. Once we've received a pair of distinct readings, update!
    if (odom_state_ == kNone) {
        odom_state_ = side;
    } else if (odom_state_ != side) {
        odom_state_ = kNone;

        // Compute the difference between the last two updates. Speed is
        // measured in RPMs, so all of these values are measured in
        // revolutions.
        double const curr_left = s16p16_to_double(odom_curr_left_);
        double const last_left = s16p16_to_double(odom_last_left_);
        double const curr_right = s16p16_to_double(odom_curr_right_);
        double const last_right = s16p16_to_double(odom_last_right_);
        double const revs_left  = curr_left - last_left;
        double const revs_right = curr_right - last_right;

        // Convert from revolutions to meters.
        double const meters_left  = revs_left * wheel_circum_;
        double const meters_right = revs_right * wheel_circum_;

        // Use the robot model to convert from wheel odometry to
        // two-dimensional motion.
        // TODO: Switch to a better odometry model.
        double const meters  = (meters_left + meters_right) / 2;
        double const radians = (meters_left - meters_right) / (2 * robot_radius_);
        x_ += meters * cos(theta_);
        y_ += meters * sin(theta_);
        theta_ = angles::normalize_angle(theta_ + radians);

        // Estimate the robot's current velocity.
        double const v_linear = (velocity_right_ + velocity_left_) / 2;
        double const omega    = (velocity_right_ - velocity_left_) / (2 * robot_radius_);

        odom_signal_(x_, y_, theta_, v_linear, omega);
    } else {
        std::cerr << "war: periodic update message was dropped" << std::endl;
    }
}

/*
 * Diagnostics
 */
void DiffDriveRobot::diag_init(void)
{
    block(
        jag_left_.periodic_config_diag(1,
            boost::bind(&DiffDriveRobot::diag_update, this,
                boost::ref(diag_left_), _1, _2, _3, _4)
        ),
        jag_right_.periodic_config_diag(1,
            boost::bind(&DiffDriveRobot::diag_update, this,
                boost::ref(diag_right_), _1, _2, _3, _4)
        )
    );

    // TODO: Make this a parameter.
    block(
        jag_left_.periodic_enable(1, 500),
        jag_right_.periodic_enable(1, 500)
    );
}

void DiffDriveRobot::diag_update(Diagnostics &diag,
    LimitStatus::Enum limits, Fault::Enum faults,
    double voltage, double temperature)
{
    // TODO: Check for a fault.
    diag.stopped = !(limits & 0x03);
    diag.voltage = voltage;
    diag.temperature = temperature;
}

/*
 * Speed Control
 */
void DiffDriveRobot::speed_set_p(double p)
{
    block(
        jag_left_.speed_set_p(p),
        jag_right_.speed_set_p(p)
    );
}

void DiffDriveRobot::speed_set_i(double i)
{
    block(
        jag_left_.speed_set_i(i),
        jag_right_.speed_set_i(i)
    );
}

void DiffDriveRobot::speed_set_d(double d)
{
    block(
        jag_left_.speed_set_d(d),
        jag_right_.speed_set_d(d)
    );
}

void DiffDriveRobot::speed_init(void)
{
    block(
        jag_left_.speed_set_reference(SpeedReference::kQuadratureEncoder),
        jag_right_.speed_set_reference(SpeedReference::kQuadratureEncoder)
    );
    block(
        jag_left_.speed_enable(),
        jag_right_.speed_enable()
    );
}

/*
 * Robot Parameters
 */
void DiffDriveRobot::robot_set_encoders(uint16_t ticks_per_rev)
{
    block(
        jag_left_.config_encoders_set(ticks_per_rev),
        jag_right_.config_encoders_set(ticks_per_rev)
    );
}

void DiffDriveRobot::robot_set_radii(double wheel_radius, double robot_radius)
{
    assert(wheel_radius > 0);
    assert(robot_radius > 0);
    wheel_circum_ = 2 * M_PI * wheel_radius;
    robot_radius_ = robot_radius;
}

/*
 * Helper Methods
 */
void DiffDriveRobot::block(can::TokenPtr t1, can::TokenPtr t2)
{
    t1->block();
    t2->block();
}

};
