/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <quackmore-ff@yahoo.com> wrote this file.  As long as you retain this notice
 * you can do whatever you want with this stuff. If we meet some day, and you 
 * think this stuff is worth it, you can buy me a beer in return. Quackmore
 * ----------------------------------------------------------------------------
 */
#ifndef __ESPBOT_EVENT_CODES_H__
#define __ESPBOT_EVENT_CODES_H__

#define FILE_TO_JSON_FS_NOT_AVAILABLE 0x0010
#define FILE_TO_JSON_FILE_NOT_FOUND 0x0011
#define FILE_TO_JSON_CANNOT_OPEN_FILE 0x0012
#define FILE_TO_JSON_HEAP_EXHAUSTED 0x0013
#define FILE_TO_JSON_CANNOT_PARSE_JSON 0x0014
#define FILE_TO_JSON_PAIR_NOT_FOUND 0x0015

#define GPIO_GETNUM_WRONG_INDEX 0x0020
#define GPIO_CONFIG_WRONG_INDEX 0x0021
#define GPIO_CONFIG_WRONG_TYPE 0x0022
#define GPIO_UNCONFIG_WRONG_INDEX 0x0023
#define GPIO_GET_CONFIG_WRONG_INDEX 0x0024
#define GPIO_READ_WRONG_INDEX 0x0025
#define GPIO_SET_WRONG_INDEX 0x0026
#define GPIO_SET_WRONG_LEVEL 0x0027
#define GPIO_SET_CANNOT_CHANGE 0x0028

#define LOGGER_RESTORE_CFG_INCOMPLETE 0x0030
#define LOGGER_RESTORE_CFG_FILE_NOT_FOUND 0x0031
#define LOGGER_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x0032
#define LOGGER_SAVE_CFG_HEAP_EXHAUSTED 0x0033
#define LOGGER_SAVE_CFG_CANNOT_OPEN_FILE 0x0034
#define LOGGER_SAVE_CFG_FS_NOT_AVAILABLE 0x0035
#define LOGGER_INIT_CFG_DEFAULT_CFG 0x0036

#define DIAG_RESTORE_CFG_INCOMPLETE 0x0037
#define DIAG_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x0038
#define DIAG_SAVE_CFG_FS_NOT_AVAILABLE 0x0039
#define DIAG_SAVE_CFG_CANNOT_OPEN_FILE 0x003A
#define DIAG_INIT_DEFAULT_CFG 0x003B

#define MDNS_START 0x0040
#define MDNS_STOP 0x0041
#define MEM_MON_HEAP_EXHAUSTED 0x0042
#define SNTP_START 0x0043
#define SNTP_STOP 0x0044
#define SNTP_CANNOT_SET_TIMEZONE 0x0045
#define UTILS_CANNOT_PARSE_IP 0x0046
#define ESPBOT_INIT_DEFAULT_CFG 0x0047
#define ESPOT_SET_NAME_TRUNCATED 0x0048
#define ESPBOT_RESTORE_CFG_INCOMPLETE 0x0049
#define ESPBOT_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x004A
#define ESPBOT_SAVE_CFG_HEAP_EXHAUSTED 0x004B
#define ESPBOT_SAVE_CFG_CANNOT_OPEN_FILE 0x004C
#define ESPBOT_SAVE_CFG_FS_NOT_AVAILABLE 0x004D

#define WIFI_CONNECTED 0x0050
#define WIFI_DISCONNECTED 0x0051
#define WIFI_AUTHMODE_CHANGE 0x0052
#define WIFI_DHCP_TIMEOUT 0x0053
#define WIFI_GOT_IP 0x0054
#define WIFI_OPMODE_CHANGED 0x0055
#define WIFI_STA_CONNECTED 0x0056
#define WIFI_STA_DISCONNECTED 0x0057
#define WIFI_CONNECT_NO_SSID_AVAILABLE 0x0058
#define WIFI_AP_SET_CH_OOR 0x0059
#define WIFI_RESTORE_CFG_FILE_NOT_FOUND 0x005A
#define WIFI_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x005B
#define WIFI_SAVE_CFG_HEAP_EXHAUSTED 0x005C
#define WIFI_SAVE_CFG_CANNOT_OPEN_FILE 0x005D
#define WIFI_SAVE_CFG_FS_NOT_AVAILABLE 0x005E
#define WIFI_INIT_CFG_DEFAULT_CFG 0x005F
#define WIFI_TRUNCATING_STRING_TO_31_CHAR 0x0060
#define WIFI_TRUNCATING_STRING_TO_63_CHAR 0x0061
#define WIFI_AP_LIST_HEAP_EXHAUSTED 0x0062
#define WIFI_AP_LIST_CANNOT_COMPLETE_SCAN 0x0063
#define WIFI_RESTORE_CFG_NO_SSID_FOUND 0x0064
#define WIFI_RESTORE_CFG_NO_PWD_FOUND 0x0065
#define WIFI_RESTORE_CFG_AP_DEFAULT_PWD 0x0066
#define WIFI_RESTORE_CFG_AP_CUSTOM_PWD 0x0067
#define WIFI_RESTORE_CFG_AP_CH 0x0068
#define WIFI_RESTORE_CFG_AP_CH_OOR 0x0069
#define WIFI_SETAP_ERROR 0x006A

#define HTTP_CLEAR_BUSY_SENDING_DATA 0x0070
#define HTTP_DELETED_PENDING_RESPONSE 0x0071
#define HTTP_JSON_ERROR_MSG_HEAP_EXHAUSTED 0x0072
#define HTTP_SEND_BUFFER_SEND_QUEUE_FULL 0x0073
#define HTTP_SEND_BUFFER_HEAP_EXHAUSTED 0x0074
#define HTTP_SEND_BUFFER_ERROR 0x0075
#define HTTP_RESPONSE_HEAP_EXHAUSTED 0x0076
#define HTTP_SEND_REMAINING_MSG_RES_QUEUE_FULL 0x0077
#define HTTP_SEND_REMAINING_MSG_HEAP_EXHAUSTED 0x0078
#define HTTP_SEND_RES_QUEUE_FULL 0x0079
#define HTTP_SEND_HEAP_EXHAUSTED 0x007A
#define HTTP_FORMAT_HEADER_HEAP_EXHAUSTED 0x007B
#define HTTP_PARSE_REQUEST_CANNOT_PARSE_EMPTY_MSG 0x007C
#define HTTP_PARSE_REQUEST_HEAP_EXHAUSTED 0x007D
#define HTTP_PARSE_REQUEST_CANNOT_FIND_HTTP_TOKEN 0x007E
#define HTTP_PARSE_REQUEST_CANNOT_FIND_ACC_CTRL_REQ_HEADERS 0x007F
#define HTTP_PARSE_REQUEST_CANNOT_FIND_ORIGIN 0x0080
#define HTTP_PARSE_REQUEST_CANNOT_FIND_CONTENT_START 0x0081
#define HTTP_PARSE_REQUEST_CANNOT_FIND_CONTENT_LEN 0x0082
#define HTTP_SAVE_PENDING_REQUEST_HEAP_EXHAUSTED 0x0083
#define HTTP_SAVE_PENDING_REQUEST_CANNOT_SAVE_REQ 0x0084
#define HTTP_CHECK_PENDING_REQUESTS_CANNOT_FIND_REQ 0x0085
#define HTTP_SAVE_PENDING_RESPONSE_HEAP_EXHAUSTED 0x0086
#define HTTP_SAVE_PENDING_RESPONSE_CANNOT_SAVE_RES 0x0087
#define HTTP_CHECK_PENDING_RESPONSE_CANNOT_FIND_RES 0x0088
#define HTTP_PARSE_RESPONSE_CANNOT_PARSE_EMPTY_MSG 0x0089
#define HTTP_PARSE_RESPONSE_HEAP_EXHAUSTED 0x008A
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_HTTP_TOKEN 0x008B
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_CONTENT_LEN 0x008C
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_CONTENT_RANGE 0x008D
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_RANGE_START 0x008E
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_RANGE_END 0x008F
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_RANGE_SIZE 0x0090
#define HTTP_PARSE_RESPONSE_CANNOT_FIND_CONTENT_START 0x0091
#define HTTP_CHECK_PENDING_SEND_QUEUE_FULL 0x0092
#define HTTP_PUSH_PENDING_SEND_QUEUE_FULL 0x0093
#define HTTP_PUSH_PENDING_SEND_HEAP_EXHAUSTED 0x0094

#define WEB_SERVER_START 0x009D
#define WEB_SERVER_STOP 0x009E
#define WEB_SERVER_EMPTY_URL 0x009F

#define ROUTES_GETAPLIST_HEAP_EXHAUSTED 0x00A0
#define ROUTES_SEND_REMAINING_MSG_HEAP_EXHAUSTED 0x00A1
#define ROUTES_SEND_REMAINING_MSG_PENDING_RES_QUEUE_FULL 0x00A2
#define ROUTES_RETURN_FILE_HEAP_EXHAUSTED 0x00A3
#define ROUTES_RETURN_FILE_PENDING_RES_QUEUE_FULL 0x00A4
#define ROUTES_PREFLIGHT_RESPONSE_HEAP_EXHAUSTED 0x00A5
#define ROUTES_GETHEXMEMDUMP_HEAP_EXHAUSTED 0x00A6
#define ROUTES_GETMEMDUMP_HEAP_EXHAUSTED 0x00A7
#define ROUTES_GETMEMINFO_HEAP_EXHAUSTED 0x00A8
#define ROUTES_GETDEVICENAME_HEAP_EXHAUSTED 0x00A9
#define ROUTES_SETDEVICENAME_HEAP_EXHAUSTED 0x00AA
#define ROUTES_GETFS_HEAP_EXHAUSTED 0x00AB
#define ROUTES_GETFILELIST_HEAP_EXHAUSTED 0x00AC
#define ROUTES_SETGPIOCFG_HEAP_EXHAUSTED 0x00AD
#define ROUTES_SETGPIOLEVEL_HEAP_EXHAUSTED 0x00AE
#define ROUTES_GETGPIOCFG_HEAP_EXHAUSTED 0x00AF
#define ROUTES_GETGPIOLEVEL_HEAP_EXHAUSTED 0x00B0
#define ROUTES_GET_API_GPIO_SET_HEAP_EXHAUSTED 0x00B1
#define ROUTES_GETOTACFG_HEAP_EXHAUSTED 0x00B2
#define ROUTES_SETOTACFG_HEAP_EXHAUSTED 0x00B3
#define ROUTES_GETWIFISTATIONCFG_HEAP_EXHAUSTED 0x00B4
#define ROUTES_SETWIFISTATIONCFG_HEAP_EXHAUSTED 0x00B5
#define ROUTES_GETWIFI_HEAP_EXHAUSTED 0x00B6
#define ROUTES_GETDIAGNOSTICEVENTS_HEAP_EXHAUSTED 0x00B7
#define ROUTES_GETDIAGNOSTICCFG_HEAP_EXHAUSTED 0x00B8
#define ROUTES_SETDIAGNOSTICCFG_HEAP_EXHAUSTED 0x00B9
#define ROUTES_GETTIMEDATE_HEAP_EXHAUSTED 0x00BA
#define ROUTES_GETTIMEDATECFG_HEAP_EXHAUSTED 0x00BB
#define ROUTES_SETTIMEDATECFG_HEAP_EXHAUSTED 0x00BC
#define ROUTES_SETTIMEDATE_HEAP_EXHAUSTED 0x00BD
#define ROUTES_GETMDNS_HEAP_EXHAUSTED 0x00BE
#define ROUTES_SETMDNS_HEAP_EXHAUSTED 0x00BF
#define ROUTES_GETCRON_HEAP_EXHAUSTED 0x00C0
#define ROUTES_SETCRON_HEAP_EXHAUSTED 0x00C1
#define ROUTES_GETLASTRESET_HEAP_EXHAUSTED 0x00C2
#define ROUTES_CONNECTWIFI_HEAP_EXHAUSTED 0x00C3
#define ROUTES_DISCONNECTWIFI_HEAP_EXHAUSTED 0x00C4
#define ROUTES_GETDIAGNOSTICEVENTS_NEXT_HEAP_EXHAUSTED 0x00C5
#define ROUTES_GETDIAGNOSTICEVENTS_NEXT_PENDING_RES_QUEUE_FULL 0x00C6
#define ROUTES_GETDIAGEVENTS_PENDING_RES_QUEUE_FULL 0x00C7
#define ROUTES_GETWIFIAPCFG_HEAP_EXHAUSTED 0x00C8
#define ROUTES_SETWIFIAPCFG_HEAP_EXHAUSTED 0x00C9

#define OTA_INIT_DEFAULT_CFG 0x00D0
#define OTA_SET_PATH_HEAP_EXHAUSTED 0x00D1
#define OTA_CANNOT_COMPLETE 0x00D2
#define OTA_TIMER_FUNCTION_USERBIN_ID_UNKNOWN 0x00D3
#define OTA_CANNOT_START_UPGRADE 0x00D4
#define OTA_SUCCESSFULLY_COMPLETED 0x00D5
#define OTA_FAILURE 0x00D6
#define OTA_REBOOTING_AFTER_COMPLETION 0x00D7
#define OTA_START_UPGRADE_CALLED_WHILE_OTA_IN_PROGRESS 0x00D8
#define OTA_RESTORE_CFG_FILE_NOT_FOUND 0x00D9
#define OTA_RESTORE_CFG_INCOMPLETE 0x00DA
#define OTA_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x00DB
#define OTA_SAVE_CFG_FS_NOT_AVAILABLE 0x00DC
#define OTA_SAVE_CFG_CANNOT_OPEN_FILE 0x00DD
#define OTA_SAVE_CFG_HEAP_EXHAUSTED 0x00FE
#define OTA_CHECK_VERSION_UNEXPECTED_WEBCLIENT_STATUS 0x00DF
#define OTA_ASK_VERSION_UNEXPECTED_WEBCLIENT_STATUS 0x00F0
#define OTA_UP_TO_DATE 0x00F1
#define OTA_ENGINE_HEAP_EXHAUSTED 0x00F2
#define OTA_CHECK_VERSION_BAD_FORMAT 0x00F3
#define OTA_CHECK_VERSION_EMPTY_RES 0x00F4

#define WEB_CLIENT_SEND_REQ_CANNOT_SEND_REQ 0x0100
#define WEB_CLIENT_ADD_CLIENT_ESPCONN_ASSOCIATION_HEAP_EXHAUSTED 0x0101
#define WEB_CLIENT_ADD_CLIENT_ASSOCIATION_REG_ERROR 0x0102
#define WEB_CLIENT_CONNECT_TIMEOUT 0x0103
#define WEB_CLIENT_SEND_REQ_TIMEOUT 0x0104
#define WEB_CLIENT_RECV_CANNOT_FIND_ESPCONN 0x0105
#define WEB_CLIENT_DISCON_CANNOT_FIND_ESPCONN 0x0106
#define WEB_CLIENT_CONNECTED_CANNOT_FIND_ESPCONN 0x0107
#define WEB_CLIENT_CONNECT_CONN_FAILURE 0x0108
#define WEB_CLIENT_SEND_REQ_HEAP_EXHAUSTED 0x0109

#define SPIFFS_INIT_CANNOT_MOUNT 0x0110
#define SPIFFS_INIT_FS_FORMATTED 0x0111
#define SPIFFS_INIT_CANNOT_FORMAT 0x0112
#define SPIFFS_INIT_FS_MOUNTED 0x0113
#define SPIFFS_INIT_FS_SIZE 0x0114
#define SPIFFS_INIT_FS_USED 0x0115
#define SPIFFS_FORMAT_FS_NOT_INIT 0x0116
#define SPIFFS_FORMAT_FS_FORMATTED 0x0117
#define SPIFFS_FORMAT_CANNOT_FORMAT 0x0118
#define SPIFFS_UNMOUNT_FS_NOT_INIT 0x0119
#define SPIFFS_UNMOUNT_FS_UNMOUNTED 0x011A
#define SPIFFS_LAST_ERROR_FS_NOT_INIT 0x011B
#define SPIFFS_GET_TOTAL_SIZE_FS_NOT_INIT 0x011C
#define SPIFFS_GET_USED_SIZE_FS_NOT_INIT 0x011D
#define SPIFFS_CHECK_FS_NOT_INIT 0x011E
#define SPIFFS_CHECK_SUCCESSFULLY 0x011F
#define SPIFFS_CHECK_ERRORS 0x0120
#define SPIFFS_GET_HANDLER_NOT_INIT 0x0121
#define SPIFFS_FFILE_FS_NOT_AVAILABLE 0x0122
#define SPIFFS_FFILE_FILE_OPEN_ERROR 0x0123
#define SPIFFS_FFILE_NAME_TRUNCATED 0x0124
#define SPIFFS_FFILE_DES_FILE_CLOSE_ERROR 0x0125
#define SPIFFS_GET_NAME_NO_NAME 0x0126
#define SPIFFS_OPEN_FILE_CLOSE_ERROR 0x0127
#define SPIFFS_OPEN_NAME_TRUNCATED 0x0128
#define SPIFFS_OPEN_FILE_OPEN_ERROR 0x0129
#define SPIFFS_OPEN_FS_NOT_AVAILABLE 0x012A
#define SPIFFS_N_READ_FILE_READ_ERROR 0x012B
#define SPIFFS_N_READ_CANNOT_READ 0x012C
#define SPIFFS_N_READ_FS_NOT_AVAILABLE 0x012D
#define SPIFFS_N_READ_FILE_SEEK_ERROR 0x012E
#define SPIFFS_N_APPEND_FILE_WRITE_ERROR 0x012F
#define SPIFFS_N_APPEND_CANNOT_WRITE 0x0130
#define SPIFFS_N_APPEND_FS_NOT_AVAILABLE 0x0131
#define SPIFFS_CLEAR_FILE_CLOSE_ERROR 0x0132
#define SPIFFS_CLEAR_FILE_OPEN_ERROR 0x0133
#define SPIFFS_CLEAR_CANNOT_CLEAR 0x0134
#define SPIFFS_CLEAR_FS_NOT_AVAILABLE 0x0135
#define SPIFFS_REMOVE_FILE_REMOVE_ERROR 0x0136
#define SPIFFS_REMOVE_CANNOT_REMOVE 0x0137
#define SPIFFS_REMOVE_FS_NOT_AVAILABLE 0x0138
#define SPIFFS_FLUSH_FILE_FLUSH_ERROR 0x0139
#define SPIFFS_FLUSH_CANNOT_FLUSH 0x013A
#define SPIFFS_FLUSH_FS_NOT_AVAILABLE 0x013B
#define SPIFFS_EXISTS_FS_NOT_AVAILABLE 0x013C
#define SPIFFS_SIZE_FS_NOT_AVAILABLE 0x013D

#define SPIFFS_FLASH_READ_OUT_OF_BOUNDARY 0x0140
#define SPIFFS_FLASH_READ_ERROR 0x0141
#define SPIFFS_FLASH_READ_TIMEOUT 0x0142
#define SPIFFS_FLASH_WRITE_OUT_OF_BOUNDARY 0x0143
#define SPIFFS_FLASH_WRITE_READ_ERROR 0x0144
#define SPIFFS_FLASH_WRITE_READ_TIMEOUT 0x0145
#define SPIFFS_FLASH_WRITE_WRITE_ERROR 0x0146
#define SPIFFS_FLASH_WRITE_WRITE_TIMEOUT 0x0147
#define SPIFFS_FLASH_ERASE_OUT_OF_BOUNDARY 0x0148
#define SPIFFS_FLASH_ERASE_ERROR 0x0149
#define SPIFFS_FLASH_ERASE_TIMEOUT 0x014A

#define TIMEDATE_RESTORE_CFG_INCOMPLETE 0x0150
#define TIMEDATE_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x0151
#define TIMEDATE_SAVE_CFG_FS_NOT_AVAILABLE 0x0152
#define TIMEDATE_SAVE_CFG_CANNOT_OPEN_FILE 0x0153
#define TIMEDATE_INIT_DEFAULT_CFG 0x0154
#define TIMEZONE_CHANGED 0x0155
#define TIMEDATE_CHANGED 0x0156

#define MDNS_RESTORE_CFG_INCOMPLETE 0x0160
#define MDNS_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x0161
#define MDNS_SAVE_CFG_FS_NOT_AVAILABLE 0x0162
#define MDNS_SAVE_CFG_CANNOT_OPEN_FILE 0x0163
#define MDNS_INIT_DEFAULT_CFG 0x0164

#define CRON_RESTORE_CFG_INCOMPLETE 0x0170
#define CRON_SAVED_CFG_NOT_UPDATED_INCOMPLETE 0x0171
#define CRON_SAVE_CFG_FS_NOT_AVAILABLE 0x0172
#define CRON_SAVE_CFG_CANNOT_OPEN_FILE 0x0173
#define CRON_INIT_DEFAULT_CFG 0x0174
#define CRON_START 0x0175
#define CRON_STOP 0x0176
#define CRON_ADD_JOB_CANNOT_COMPLETE 0x0177
#define CRON_ADD_JOB_HEAP_EXHAUSTED 0x0178
#define CRON_ENABLED 0x0179
#define CRON_DISABLED 0x017A

#endif