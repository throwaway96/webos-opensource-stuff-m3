// (c) Copyright 2013 LG Electronics, Inc.

#include "luna_svc_mgr.h"
#include "luna_helper.h"
#include "til_handler.h"

#include <glibmm.h>
#include <stdio.h>
#include "logging.h"

extern LunaIpcHelper lunaCm;

ServiceMgr ServiceMgr::mSrvMgr;

ServiceMgr::ServiceMgr() : mpMainLoop(Glib::MainLoop::create())
{
    TDS_LOG_DEBUG("TDS");
}

ServiceMgr::~ServiceMgr() {
    TDS_LOG_DEBUG("TDS");
    stop();
}

void ServiceMgr::registerServices() {
    bool retVal = false;

    if (mpMainLoop) {
        retVal = lunaCm.svcRegister(mpMainLoop->gobj());

        if (retVal)
            retVal = lunaCm.watchServiceStatus("com.palm.telephony", til_luna_ServiceStatusHandler);
    }
    else {
        TDS_LOG_DEBUG("Error occurred during initiate service");
    }
}

bool ServiceMgr::start() {
    TDS_LOG_DEBUG("Starting TelephonyDataService mpMainLoop");

    bool didStartOk = false;

    if (mpMainLoop) {
            didStartOk = true;
            mpMainLoop->run();
    }

    stop();

    return (didStartOk);
}

bool ServiceMgr::stop() {
    if (mpMainLoop) {
        mpMainLoop->unreference();
        mpMainLoop.reset();
    }
    return (true);
}
