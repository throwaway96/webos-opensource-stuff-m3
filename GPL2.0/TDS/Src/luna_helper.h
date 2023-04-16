// (c) Copyright 2013 LG Electronics, Inc.

#include <lunaservice.h>
#include <glibmm.h>
#include <string>

#include <pbnjson.hpp>

using namespace std;

#define JSON_STD_SUCCESS "{\"returnValue\":true}"
#define JSON_STD_FAILURE "{\"returnValue\":false,\"errorCode\":-1,\"errorText\":\"Unknown Error\"}"

#define UNKOWN_ERROR (-1)
#define UNKOWN_ERROR_TEXT "Unknown Error"

#define SUBSCRIBE_ERROR (-2)
#define SUBSCRIBE_ERROR_TEXT "Subscribe Failed"

#define LUNA_CMD_NULL { }

#define GET_DOM_FROM_LSMESSAGE(x, y) getDomFromLsMessage(x, y, __func__)
#define REPLY(x, y, z) reply(x, y, z, __func__)

class LunaIpcHelper {
protected:
    LSHandle* spLsPrvH;

    LunaIpcHelper(const LunaIpcHelper&);

    LunaIpcHelper& operator=(const LunaIpcHelper&);

public:
    string mDomain;

    LunaIpcHelper(const char* domain);
    virtual ~LunaIpcHelper();

    virtual bool svcRegister(GMainLoop* aMainLoop);
    virtual bool svcUnregister();
    virtual bool svcRegisterCategories(const char* lunaPath, LSMethod* methods);
    virtual bool watchServiceStatus(const char* serviceName, LSServerStatusFunc callback);
    virtual bool sendPrivate(const char* uri, const pbnjson::JValue& jsonObj, LSFilterFunc callback, void *ctx = NULL);
    virtual bool sendPrivate(const char* uri, const char* payload, LSFilterFunc callback);
    virtual bool sendPrivate(const char* uri, const char* payload, LSFilterFunc callback, void *ctx);
    virtual bool reply(LSHandle* hLsHandle, LSMessage *pMessage, pbnjson::JValue& jsonObj, const char* callingFunction);
    virtual bool reply(LSHandle* hLsHandle, LSMessage *pMessage, const char* payload, const char* callingFunction);
    virtual bool reply(LSMessage *pMessage, pbnjson::JValue& jsonObj);

    virtual pbnjson::JValue getDomFromLsMessage(LSMessage *pMessage, std::string& payload, const char* callingFn = NULL);
    virtual void genStdErrReturn(pbnjson::JValue& jsonObj, bool isOk, int errCode = 0, const Glib::ustring& errString = "");
    virtual Glib::ustring genStdErrReturn(bool isOk, int errCode = 0, const Glib::ustring& errString = "");

    virtual LSHandle* getPrivateHandle() {
        return (spLsPrvH);
    }

protected:
    virtual bool svcRegisterDomain();
    virtual bool attachToMainLoop(GMainLoop* aMainLoop);
};

void lunaLogError(LSError& err);
