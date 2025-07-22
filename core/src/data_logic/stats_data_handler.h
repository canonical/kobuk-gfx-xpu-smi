/* 
 *  Copyright (C) 2021-2024 Intel Corporation
 *  SPDX-License-Identifier: MIT
 *  @file metric_statistics_data_handler.h
 */

#pragma once

#include "data_handler.h"

namespace xpum {

struct Statistics_subdevice_data {
    uint64_t count;
    uint64_t avg;
    uint64_t min;
    uint64_t max;
    Statistics_subdevice_data(uint64_t data) {
        min = data;
        max = data;
        avg = data;
        count = 1;
    }
};

struct Statistics_data {
    uint64_t count;
    uint64_t avg;
    uint64_t min;
    uint64_t max;
    long long start_time;
    long long latest_time;
    bool hasDataOnDevice;
    std::map<uint32_t, Statistics_subdevice_data> subdevice_datas;
    Statistics_data(uint64_t data, long long time) {
        min = data;
        max = data;
        avg = data;
        count = 1;
        start_time = time;
        latest_time = time;
        hasDataOnDevice = true;
    }
    Statistics_data(uint32_t subdevice_id, uint64_t data, long long time) {
        subdevice_datas.insert(std::make_pair(subdevice_id, Statistics_subdevice_data(data)));
        min = 0;
        max = 0;
        avg = 0;
        count = 0;
        start_time = time;
        latest_time = time;
        hasDataOnDevice = false;
    }
};

class StatsDataHandler : public DataHandler {
   public:
    StatsDataHandler(MeasurementType type, std::shared_ptr<Persistency> &p_persistency);

    virtual ~StatsDataHandler();

    virtual void handleData(std::shared_ptr<SharedData> &p_data) noexcept;

    virtual std::shared_ptr<MeasurementData> getLatestStatistics(std::string &device_id, uint64_t session_id) noexcept;

   protected:
    void resetStatistics(std::string &device_id, uint64_t session_id);

    void updateStatistics(std::shared_ptr<SharedData> &p_data);

    //The map index is session ID, the second map index is device ID
    std::map<uint64_t, std::map<std::string, Statistics_data>> multi_sessions_data;
};
} // end namespace xpum
