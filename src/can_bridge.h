#ifndef CANBRIDGE_H_
#define CANBRIDGE_H_

#include <exception>
#include <string>
#include <vector>

namespace can {

class CANBridge {
public:
    virtual void send(uint32_t id, void const *data, size_t length) = 0;
};

class CANException : public std::exception {
public:
    CANException(std::string what) : m_what(what) {}
    CANException(int code, std::string what) : m_code(code), m_what(what) {}
	virtual ~CANException(void) throw() {}
    virtual char const* what() const throw() { return m_what.c_str(); }
    virtual int code() const throw() { return m_code; }

private:
    int m_code;
    std::string m_what;
};

};

#endif
