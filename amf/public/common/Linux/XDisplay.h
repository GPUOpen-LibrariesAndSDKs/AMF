#pragma once
#include <memory>
#include <X11/X.h>
#include <X11/Xlib.h>

//this pattern makes it impossible to use the x11 Display* pointer without first calling XLockDisplay
class XDisplay {
public:
    typedef std::shared_ptr<XDisplay> Ptr;

    XDisplay() : m_pDisplay(XOpenDisplay(nullptr)) {}
    ~XDisplay() { if(IsValid()) XCloseDisplay(m_pDisplay); }

    bool IsValid() { return m_pDisplay != nullptr; }

private:
    Display* m_pDisplay;
    friend class XDisplayPtr;
};

class XDisplayPtr {
public:

    XDisplayPtr() = delete;
    XDisplayPtr(const XDisplayPtr&) = delete;
    XDisplayPtr& operator=(const XDisplayPtr&) =delete;

    explicit XDisplayPtr(std::shared_ptr<XDisplay> display) : m_pDisplay(display) { XLockDisplay(m_pDisplay->m_pDisplay); }
    ~XDisplayPtr() { XUnlockDisplay(m_pDisplay->m_pDisplay); }

    //XDisplayPtr acts like a normal Display* pointer, but the only way to obtain it is by locking the Display
    operator Display*() { return m_pDisplay->m_pDisplay; }

private:
    XDisplay::Ptr m_pDisplay;
};
