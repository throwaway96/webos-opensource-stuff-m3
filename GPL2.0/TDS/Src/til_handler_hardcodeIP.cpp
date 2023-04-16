// (c) Copyright 2013 LG Electronics, Inc.

#include "til_handler.h"
#include "gprs.h"
#include "luna_helper.h"
#include "logging.h"

#include <pbnjson.hpp>
#include <lunaservice.h>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <boost/format.hpp>
#include <set>
#include <stdio.h>
#include <stdbool.h>
#include <iostream>

using namespace std;
using namespace pbnjson;

extern LunaIpcHelper lunaCm;
extern pri_context *g_pri_context;

struct tds_context *tds_data = new tds_context;

bool parseReturnValue(JValue& jsonRoot) {
    TDS_LOG_DEBUG("TDS");

    bool isReturnValue = false;
    if(!jsonRoot.isNull() && jsonRoot["returnValue"].isBoolean()){
        isReturnValue = jsonRoot["returnValue"].asBool();
    }
    TDS_LOG_DEBUG("isReturnValue = %s", isReturnValue?"true":"false");

    const int errCode = (!jsonRoot.isNull() && jsonRoot["errorCode"].isNumber()) ? jsonRoot["errorCode"].asNumber<int> () : 0;
    TDS_LOG_DEBUG("errorCode = %d", errCode);

    string errString;
    if (jsonRoot["errorString"].isString()) {
        errString = jsonRoot["errorString"].asString();
        TDS_LOG_DEBUG("errorString = %s", errString.c_str());
    }

    return isReturnValue;
}

bool parseSubscribed(JValue& jsonRoot) {
    TDS_LOG_DEBUG("TDS");

    bool subscribed = false;
    if(!jsonRoot.isNull() && jsonRoot["subscribed"].isBoolean()){
        subscribed = jsonRoot["subscribed"].asBool();
    }
    TDS_LOG_DEBUG("subscribed = %s", subscribed?"true":"false");

    return subscribed;
}

bool til_resp_ConnectDataService(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("%p", ctx);

    bool ReqResult = false;
    string payload;

    pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
    if (!jsonRoot.isNull()) {
        TDS_LOG_DEBUG("%s", payload.c_str());
        ReqResult = parseReturnValue(jsonRoot);
    } else {
        TDS_LOG_DEBUG("NULL PAYLOAD!");
    }

    til_handle_Activate(ReqResult, ctx);

    return true;
}

bool til_luna_ConnectDataService(void *ctx_data) {
    TDS_LOG_DEBUG("%p", ctx_data);

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/pdpActivate", params, til_resp_ConnectDataService, ctx_data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

bool til_resp_DisconnectDataService(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("TDS");

    bool ReqResult = false;
    string payload;

    pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
    if (!jsonRoot.isNull()) {
        TDS_LOG_DEBUG("%s", payload.c_str());
        ReqResult = parseReturnValue(jsonRoot);
    } else {
        TDS_LOG_DEBUG("NULL PAYLOAD");
    }

    /* Update ConnectionContext's Active Status */
    til_handle_Deactivate(ReqResult, ctx);

    return true;
}

bool til_luna_DisconnectDataService(void *ctx_data) {
    TDS_LOG_DEBUG("START");

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/pdpDeactivate", params, til_resp_DisconnectDataService, ctx_data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

string parseReturnValue_DataStatus(JValue& jsonRoot) {

    TDS_LOG_DEBUG("TDS");

    string connState;
    if (jsonRoot["state"].isString()) {
        connState = jsonRoot["state"].asString();
        TDS_LOG_DEBUG("pdp state = %s", connState.c_str());
    }

    return connState;
}

bool til_resp_DataConnectionStatus(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("TDS");

    bool ReqResult = false;
    string connState;
    string payload;

    pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
    if (!jsonRoot.isNull()) {
        TDS_LOG_DEBUG("%s", payload.c_str());

        ReqResult = parseReturnValue(jsonRoot);
        if(!ReqResult){
            til_handle_CommonError(ReqResult, ctx);
        }
        else{
            connState = parseReturnValue_DataStatus(jsonRoot);
        }
    } else {
        TDS_LOG_DEBUG("NULL PAYLOAD!");
    }

    if(ReqResult){
        til_handle_ConnStatus(connState, ctx);
    }

    return true;
}

bool til_luna_DataConnectionStatusQuery(void *ctx_data){
    TDS_LOG_DEBUG("START");

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());
    params.put("subscribe", true);

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/dataConnectionStatusQuery", params, til_resp_DataConnectionStatus, ctx_data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

bool parseReturnValue_radioConnected(JValue& extObj) {
    TDS_LOG_DEBUG("TDS");

    bool radioConnected;
    if (!extObj.isNull() && extObj["radioConnected"].isBoolean()) {
        radioConnected = extObj["radioConnected"].asBool();
        TDS_LOG_DEBUG("radioConnected = %s", radioConnected?"true":"false");
    }

    return radioConnected;
}

bool parseReturnValue_emergency(JValue& extObj) {
    TDS_LOG_DEBUG("TDS");

    bool emergency;
    if (!extObj.isNull() && extObj["emergency"].isBoolean()) {
        emergency = extObj["emergency"].asBool();
        TDS_LOG_DEBUG("emergency = %s", emergency?"true":"false");
    }

    return emergency;
}

bool parseReturnValue_power(JValue& extObj) {
    TDS_LOG_DEBUG("TDS");

    bool power;
    if (!extObj.isNull() && extObj["power"].isBoolean()) {
        power = extObj["power"].asBool();
        TDS_LOG_DEBUG("power = %s", power?"true":"false");
    }

    return power;
}

bool til_resp_IsTelephonyReady(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("TDS");

    string payload;
    bool ReqResult = false;
    bool power = false;
    bool radioConnected = false;
    bool emergency = false;

    pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
    if (!jsonRoot.isNull()) {
        TDS_LOG_DEBUG("%s", payload.c_str());
        ReqResult = parseReturnValue(jsonRoot);

        if(!ReqResult){
            til_handle_CommonError(ReqResult, ctx);
        }
        else{
            pbnjson::JValue extObj;
            if(!jsonRoot["extended"].isNull()){
                extObj = jsonRoot["extended"];
            }
            else if(!jsonRoot["eventNetwork"].isNull()){
                extObj = jsonRoot["eventNetwork"];
            }
            else{
                TDS_LOG_DEBUG("extObj is NULL PAYLOAD!");
            }

            radioConnected = parseReturnValue_radioConnected(extObj);
            emergency = parseReturnValue_emergency(extObj);
            power = parseReturnValue_power(extObj);
        }
    } else {
        TDS_LOG_DEBUG("NULL PAYLOAD!");
    }

    if(ReqResult){
        til_handle_OnlineStatus(radioConnected, ctx);

        til_handle_EmergencyStatus(emergency, ctx);

        til_handle_PowerStatus(power, ctx);
    }

    return true;
}

bool til_luna_IsTelephonyReady(void *ctx_data){
    TDS_LOG_DEBUG("TDS");

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());
    params.put("subscribe", true);

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/isTelephonyReady", params, til_resp_IsTelephonyReady, ctx_data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

bool parseReturnValue_dataRegistered(JValue& extObj) {
    TDS_LOG_DEBUG("TDS");

    bool dataRegistered;
    if (!extObj.isNull() && extObj["dataRegistered"].isBoolean()) {
        dataRegistered = extObj["dataRegistered"].asBool();
        TDS_LOG_DEBUG("dataRegistered = %s", dataRegistered?"true":"false");
    }

    return dataRegistered;
}

string parseReturnValue_dataType(JValue& extObj) {
    TDS_LOG_DEBUG("TDS");

    string dataType;
    if (!extObj.isNull() && extObj["dataType"].isString()) {
        dataType = extObj["dataType"].asString();
        TDS_LOG_DEBUG("dataType = %s", dataType.c_str());
    }

    return dataType;
}

bool til_resp_NetworkStatusQuery(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("TDS");

    string payload;
    bool ReqResult = false;
    bool dataRegistered = false;
    string dataType;

    pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
    if (!jsonRoot.isNull()) {
        TDS_LOG_DEBUG("%s", payload.c_str());
        ReqResult = parseReturnValue(jsonRoot);

        if(!ReqResult){
            til_handle_CommonError(ReqResult, ctx);
        }
        else{
            pbnjson::JValue extObj;
            if(!jsonRoot["extended"].isNull()){
                extObj = jsonRoot["extended"];
            }
            else if(!jsonRoot["eventNetwork"].isNull()){
                extObj = jsonRoot["eventNetwork"];
            }
            else{
                TDS_LOG_DEBUG("extObj is NULL PAYLOAD!");
            }

            dataRegistered = parseReturnValue_dataRegistered(extObj);
            dataType = parseReturnValue_dataType(extObj);
        }
    } else {
        TDS_LOG_DEBUG("NULL PAYLOAD!");
    }

    if(ReqResult){
        til_handle_AttachedStatus(dataRegistered, ctx);

        til_handle_Bearer(dataType, ctx);
    }

    return true;
}

bool til_luna_NetworkStatusQuery(void *ctx_data){
    TDS_LOG_DEBUG("TDS");

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());
    params.put("subscribe", true);

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/networkStatusQuery", params, til_resp_NetworkStatusQuery, ctx_data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

bool til_resp_ApnModify(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("TDS");

    struct tds_context *data = (struct tds_context *) ctx;
    struct pri_context *ctx_data = (struct pri_context *) data->pre_context;

    bool ReqResult = false;
    string payload;

    pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
    if (!jsonRoot.isNull()) {
        TDS_LOG_DEBUG("%s", payload.c_str());
        ReqResult = parseReturnValue(jsonRoot);

        if(!ReqResult){
            til_handle_ApnSet(ReqResult, ctx_data);
            return false;
        }
        else{
            strcpy(ctx_data->context.apn, data->apn_set);
            til_handle_ApnSet(ReqResult, ctx_data);
        }
    } else {
        TDS_LOG_DEBUG("NULL PAYLOAD");
    }

    return true;
}

bool til_luna_ApnModify(void *data) {
    TDS_LOG_DEBUG("TDS");

    struct tds_context *ctx = (struct tds_context *) data;

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());
    params.put("slot", 1);
    params.put("profilename", "myprofile");
    params.put("apn", ctx->apn_set);

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/pdpModifyProfile", params, til_resp_ApnModify, data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

string parseApnUpdateResult(JValue& jsonRoot) {
    TDS_LOG_DEBUG("TDS");

    pbnjson::JValue extObj = jsonRoot["extended"];

    string prof_name;
    if ( !(extObj["profilename"].isString()) ) {
        TDS_LOG_DEBUG("profilename: It's not a string!");
    }
    else{
        prof_name = extObj["profilename"].asString();
        TDS_LOG_DEBUG("profilename = %s", prof_name.c_str());
    }

    string apn_til;
    if ( !(extObj["apn"].isString()) ) {
        TDS_LOG_DEBUG("apn: It's not a string!");
    }
    else{
        apn_til = extObj["apn"].asString();
        TDS_LOG_DEBUG("apn = %s", apn_til.c_str());
    }

    string u_name;
    if ( !(extObj["username"].isString()) ) {
        TDS_LOG_DEBUG("username: It's not a string!");
    }
    else{
        u_name = extObj["username"].asString();
        TDS_LOG_DEBUG("username = %s", u_name.c_str());
    }

    return apn_til;
}

bool til_resp_ApnUpdate(LSHandle *sh, LSMessage *reply, void *ctx) {
    TDS_LOG_DEBUG("TDS");

    struct tds_context *data = (struct tds_context *) ctx;
    struct pri_context *ctx_data = (struct pri_context *) data->pre_context;

    TDS_LOG_DEBUG("data->apn_set = %s, ctx_data->active = %d", data->apn_set, ctx_data->active );

	bool ReqResult = false;
	string apn_til;
	string payload;

	pbnjson::JValue jsonRoot = lunaCm.GET_DOM_FROM_LSMESSAGE(reply, payload);
	if (!jsonRoot.isNull()) {
		TDS_LOG_DEBUG("%s", payload.c_str());
		ReqResult = parseReturnValue(jsonRoot);

		if(!ReqResult){
			til_handle_ApnSet(ReqResult, ctx_data);
			return false;
		}
		else
		{
			apn_til = parseApnUpdateResult(jsonRoot);
		}
	} else {
		TDS_LOG_DEBUG("NULL PAYLOAD!");
	}

	string str_apn(data->apn_set);
	if(str_apn.compare(apn_til)!=0){
		strcpy(ctx_data->context.apn,(const char *)apn_til.c_str());
		data->apn_set = ctx_data->context.apn;
		til_handle_ApnSet(ReqResult, ctx_data);
	}


    return true;
}

bool til_luna_ApnUpdate(void *ctx_data, const char *apn_data) {
    TDS_LOG_DEBUG("TDS");

    struct tds_context *data = tds_data;
    data->pre_context = (struct pri_context *) ctx_data;

	data->apn_set = apn_data;

    TDS_LOG_DEBUG("ctx->apn_set = [%s]", data->apn_set );

    bool isSuccess = false;
    pbnjson::JValue params(pbnjson::Object());
    params.put("slot", 1);

    isSuccess = lunaCm.sendPrivate("palm://com.palm.telephony/pdpProfileQuery", params, til_resp_ApnUpdate, data);

    if (!isSuccess) {
        TDS_LOG_DEBUG("Failed to connect service!");
    }

    return isSuccess;
}

bool til_luna_ServiceStatusHandler(LSHandle *sh, const char *serviceName, bool connected, void *ctx_data) {
    TDS_LOG_DEBUG("connected = %s, serviceName =  %s", (connected) ? "true" : "false", serviceName );

    struct pri_context *data_init = g_pri_context;
    bool isSuccess = false;

    if (connected) {
		isSuccess = til_luna_IsTelephonyReady(data_init);
        if (!isSuccess) {
            TDS_LOG_DEBUG("Failed to register getstatus - tilIpcIsTelephonyReady!");
        }

		isSuccess = til_luna_NetworkStatusQuery(data_init);
        if (!isSuccess) {
            TDS_LOG_DEBUG("Failed to register getstatus - tilIpcNetworkStatusQuery!");
        }

		TDS_LOG_DEBUG("Quering APN Info - til_luna_ApnUpdate!");

		if(strlen(data_init->context.apn))
		{
			isSuccess = til_luna_ApnUpdate(data_init, data_init->context.apn);
		}
		else
		{
	    	strcpy(data_init->context.apn,"VirtualAPN.com");
			isSuccess = til_luna_ApnUpdate(data_init,data_init->context.apn);
		}

        if (!isSuccess) {
            TDS_LOG_DEBUG("Failed to Query APN - til_luna_ApnUpdate!");
			return true;
        }

    }
    return true;
}
