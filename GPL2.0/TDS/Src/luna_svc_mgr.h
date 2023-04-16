// (c) Copyright 2013 LG Electronics, Inc.
#include <glibmm.h>
#include <boost/utility.hpp>

class ServiceMgr {
private:
    static ServiceMgr mSrvMgr;

    Glib::RefPtr<Glib::MainLoop> mpMainLoop;

private:
    ServiceMgr();
    virtual ~ServiceMgr();

    void operator delete(void*, size_t) {
        ;
    }

public:
    inline static ServiceMgr& getInstance() {
        return (mSrvMgr);
    }

    virtual bool start();

    virtual bool stop();

    virtual void registerServices();
};

