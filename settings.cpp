#include "stdafx.h"
#include "settings.h"

// {FEB71FC5-59E1-495D-A7E5-E4B49F7D9D95}
static const GUID guid_cfg_station_url =
{ 0xfeb71fc5, 0x59e1, 0x495d, { 0xa7, 0xe5, 0xe4, 0xb4, 0x9f, 0x7d, 0x9d, 0x95 } };

// {EF73A35D-3523-4036-9489-664A402A48FF}
static const GUID guid_cfg_poll_interval =
{ 0xef73a35d, 0x3523, 0x4036, { 0x94, 0x89, 0x66, 0x4a, 0x40, 0x2a, 0x48, 0xff } };


cfg_string cfg_station_url(guid_cfg_station_url,
    "");

cfg_int cfg_poll_interval(guid_cfg_poll_interval, 10000);
