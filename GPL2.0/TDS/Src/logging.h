/* @@@LICENSE
*
*      Copyright (c) 2012 Hewlett-Packard Development Company, L.P.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
* LICENSE@@@ */

#ifndef _LOGGING_H_
#define _LOGGING_H_

#include <stdlib.h>
#include <PmLogLib.h>

#define TDS_LOG_MSG_ID    "telephonydataservice"

extern PmLogContext gLogContext;

/**
 * TDS_LOG_XXXX Usage Guidelines
 *
 * The following comments are a set of guidelines for deciding
 * which log level to use for a particular event of interest. To
 * enable a predictable experience when debugging, it's
 * important to use the logging levels consistently.
 *
 * TDS_LOG_DEBUG: (Linux mapping: debug) Almost everything that
 * is of interest to log should be logged at the DEBUG level;
 * NOTE: this level will normally be disabled in production at
 * PmLogLib level, but will still incur a fair amount of
 * overhead in PmLogLib's atomic check of the logging context.
 *
 * TDS_LOG_INFO: (Linux mapping: info) Informational;
 *
 * TDS_LOG_NOTICE: (Linux mapping: notice) Normal, but
 * significant condition
 *
 * TDS_LOG_WARNING: (Linux mapping: warning) Warning conditions;
 *
 * TDS_LOG_ERROR: (Linux mapping: err); Error condition
 *
 * TDS_LOG_CRITICAL: (Linux mapping: crit); Critical condition.
 *
 * TDS_LOG_FATAL: (Linux mapping: crit); Fatal condition,
 * will also abort the process.
 */

#define TDS_LOG_DEBUG(...) \
	PmLogDebug(gLogContext, __VA_ARGS__)

#define TDS_LOG_INFO(...) \
	PmLogInfo(gLogContext, TDS_LOG_MSG_ID, 0, __VA_ARGS__)

#define TDS_LOG_WARNING(...) \
	PmLogWarning(gLogContext, TDS_LOG_MSG_ID, 0, __VA_ARGS__)

#define TDS_LOG_ERROR(...) \
	PmLogError(gLogContext, TDS_LOG_MSG_ID, 0, __VA_ARGS__)

#define TDS_LOG_CRITICAL(...) \
	PmLogCritical(gLogContext, TDS_LOG_MSG_ID, 0, __VA_ARGS__)

//fprintf (stderr, __VA_ARGS__)


#endif // _LOGGING_H_
