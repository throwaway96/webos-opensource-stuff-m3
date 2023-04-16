// (c) Copyright 2013 LG Electronics, Inc.

#include "luna_helper.h"
#include "logging.h"

#include <lunaservice.h>
#include <string>
#include <pbnjson.hpp>
#include <stdio.h>

using namespace std;

void lunaLogError(LSError& err) {
    TDS_LOG_ERROR("LUNASERVICE ERROR %d: %s (%s @ %s:%d)\n", err.error_code, err.message, err.func, err.file, err.line);
}

LunaIpcHelper::LunaIpcHelper(const char* domain) :
    spLsPrvH(NULL), mDomain(domain) {
    ;
}

LunaIpcHelper::~LunaIpcHelper() {
    svcUnregister();
}

bool LunaIpcHelper::svcRegisterDomain() {
    LSError lserror;
    LSErrorInit(&lserror);

    bool isSuccess = LSRegister((const char *)mDomain.c_str(), &spLsPrvH, &lserror);
    if (!isSuccess) {
        lunaLogError(lserror);
        LSErrorFree(&lserror);
    }

    return isSuccess;
}

bool LunaIpcHelper::svcRegister(GMainLoop* aMainLoop) {
    bool isSuccess = true;

    if (aMainLoop && svcRegisterDomain() && spLsPrvH) {
        isSuccess = attachToMainLoop(aMainLoop);
        if (!isSuccess) {
            svcUnregister();
            TDS_LOG_DEBUG("TDS");
        }
    } else {
        isSuccess = false;
    }

    return isSuccess;
}

bool LunaIpcHelper::svcUnregister() {
    LSError lserror;
    LSErrorInit(&lserror);
    bool isSuccess = true;

    if (spLsPrvH) {
        isSuccess = LSUnregister(spLsPrvH, &lserror);
        if (!isSuccess) {
            lunaLogError(lserror);
            LSErrorFree(&lserror);
        }
        spLsPrvH = NULL;
    }

    return isSuccess;
}

bool LunaIpcHelper::attachToMainLoop(GMainLoop* aMainLoop) {
    LSError lserror;
    LSErrorInit(&lserror);
    g_assert(spLsPrvH);

    bool isSuccess = LSGmainAttach(spLsPrvH, aMainLoop, &lserror);
    if (!isSuccess) {
        lunaLogError(lserror);
        LSErrorFree(&lserror);
    }

    return isSuccess;
}

bool LunaIpcHelper::watchServiceStatus(const char* serviceName, LSServerStatusFunc callback) {
    LSError lserror;
    LSErrorInit(&lserror);

    bool isSuccess = LSRegisterServerStatus(spLsPrvH, serviceName, callback, NULL, &lserror);
    if (!isSuccess) {
        lunaLogError(lserror);
        LSErrorFree(&lserror);
    }

    return isSuccess;
}

bool LunaIpcHelper::svcRegisterCategories(const char* lunaPath, LSMethod* methods) {
    LSError lserror;
    LSErrorInit(&lserror);

    bool isSuccess = LSRegisterCategory(spLsPrvH, lunaPath, methods, NULL, NULL, &lserror);
    if (!isSuccess) {
        lunaLogError(lserror);
        LSErrorFree(&lserror);
    }

    return isSuccess;
}

bool LunaIpcHelper::sendPrivate(const char* uri, const char* payload, LSFilterFunc callback, void *ctx) {
    LSError lserror;
    LSErrorInit(&lserror);

    bool isSuccess = LSCall(spLsPrvH, uri, payload, callback, ctx, NULL, &lserror);
    if (!isSuccess) {
        lunaLogError(lserror);
        LSErrorFree(&lserror);
    }

    return isSuccess;
}

bool LunaIpcHelper::sendPrivate(const char* uri, const char* payload, LSFilterFunc callback) {
    bool ret = false;
    ret = sendPrivate(uri, payload, callback, NULL);
    return ret;
}

bool LunaIpcHelper::sendPrivate(const char* uri, const pbnjson::JValue& jsonObj, LSFilterFunc callback, void *ctx) {
    bool retVal = false;
    pbnjson::JGenerator serializer(NULL);
    string jsonOut;

    if (serializer.toString(jsonObj, pbnjson::JSchema::NullSchema(), jsonOut)) {
        retVal = sendPrivate(uri, (const char *)jsonOut.c_str(), callback, ctx);
    }

    return retVal;
}

bool LunaIpcHelper::reply(LSHandle* hLsHandle, LSMessage *pMessage, const char* payload, const char* callingFunction) {
    LSError lserror;
    LSErrorInit(&lserror);
    bool isSuccess = LSMessageReply(hLsHandle, pMessage, payload, &lserror);

    if (!isSuccess) {
        lunaLogError(lserror);
        LSErrorFree(&lserror);
    }

    return isSuccess;
}

bool LunaIpcHelper::reply(LSHandle* hLsHandle, LSMessage *pMessage, pbnjson::JValue& jsonObj, const char* callingFunction) {
    bool retVal = false;
    pbnjson::JGenerator serializer(NULL);
    string jsonOut;

    if (serializer.toString(jsonObj, pbnjson::JSchema::NullSchema(), jsonOut)) {
        retVal = reply(hLsHandle, pMessage, (const char *)jsonOut.c_str(), callingFunction);
    }

    return retVal;
}

bool LunaIpcHelper::reply(LSMessage *pMessage, pbnjson::JValue& jsonObj) {
    pbnjson::JGenerator serializer(NULL);
    string jsonOut;
    bool isSuccess = false;

    if (serializer.toString(jsonObj, pbnjson::JSchema::NullSchema(), jsonOut)) {
        isSuccess = reply(spLsPrvH, pMessage, (const char *)jsonOut.c_str(), __func__);
    }

    return isSuccess;
}

void LunaIpcHelper::genStdErrReturn(pbnjson::JValue& jsonObj, bool isOk, int errCode, const Glib::ustring& errString) {
    jsonObj.put("returnValue", isOk);
    if (!isOk) {
        jsonObj.put("errorCode", errCode);
        if (!errString.empty()) {
            jsonObj.put("errorText", errString.raw());
        }
    }
}

Glib::ustring LunaIpcHelper::genStdErrReturn(bool isOk, int errCode, const Glib::ustring& errString) {
    std::string returnJsonStr;
    pbnjson::JValue jsonObj(pbnjson::Object());
    genStdErrReturn(jsonObj, isOk, errCode, errString);

    pbnjson::JGenerator serializer(NULL);
    if (!serializer.toString(jsonObj, pbnjson::JSchema::NullSchema(), returnJsonStr)) {
        TDS_LOG_DEBUG("%s: Failed converting JSON", __func__);
        returnJsonStr = JSON_STD_FAILURE;
    }

    return returnJsonStr;
}

pbnjson::JValue LunaIpcHelper::getDomFromLsMessage(LSMessage *pMessage, std::string &payload, const char* callingFn) {
    payload = LSMessageGetPayload(pMessage);
    pbnjson::JDomParser parser(NULL);
    pbnjson::JValue dom;

    if (!payload.empty() && parser.parse(payload, pbnjson::JSchema::NullSchema(), NULL)) {
        if (callingFn) {
            TDS_LOG_DEBUG("%s Parsing:  %s", (callingFn ? callingFn : "Fn Unk"), payload.c_str());
        }
        dom = parser.getDom();
    } else {
        TDS_LOG_DEBUG("%s Failed to parse JSON: %s", (callingFn ? callingFn : "Fn Unk"), (payload.empty() ? "NULL PAYLOAD" : payload.c_str()));
    }

    return dom;
}

