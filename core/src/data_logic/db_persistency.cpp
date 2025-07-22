/* 
 *  Copyright (C) 2021-2024 Intel Corporation
 *  SPDX-License-Identifier: MIT
 *  @file db_persistency.cpp
 */

#include "db_persistency.h"

#include "infrastructure/logger.h"

namespace xpum {

void DBPersistency::storeData2PersistentStorage(
    MeasurementType type, Timestamp_t time,
    std::map<std::string, std::shared_ptr<MeasurementData>> &datas) {
    XPUM_LOG_TRACE("received monitor data, type: {}", type);
}

} // end namespace xpum
