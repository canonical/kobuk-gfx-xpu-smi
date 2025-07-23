/* 
 *  Copyright (C) 2021-2024 Intel Corporation
 *  SPDX-License-Identifier: MIT
 *  @file time_weighted_average_data_handler.cpp
 */

#include "time_weighted_average_data_handler.h"

#include <algorithm>
#include <iostream>

namespace xpum {

TimeWeightedAverageDataHandler::TimeWeightedAverageDataHandler(MeasurementType type,
                                                               std::shared_ptr<Persistency>& p_persistency)
    : StatsDataHandler(type, p_persistency) {
}

TimeWeightedAverageDataHandler::~TimeWeightedAverageDataHandler() {
    close();
}

void TimeWeightedAverageDataHandler::counterOverflowDetection(std::shared_ptr<SharedData>& p_data) noexcept {
    std::unique_lock<std::mutex> lock(this->mutex);
    if (p_preData == nullptr || p_data == nullptr) {
        return;
    }

    std::map<std::string, std::shared_ptr<MeasurementData>>::iterator iter = p_data->getData().begin();
    while (iter != p_data->getData().end()) {
        auto &deviceId = iter->first;
        auto &measurementData = iter->second;
        if (measurementData->hasRawDataOnDevice() 
        && p_preData->getData().find(deviceId) != p_preData->getData().end()
        && p_preData->getData()[deviceId]->hasRawDataOnDevice()
        && p_data->getData().find(deviceId) != p_data->getData().end()) {
            uint64_t pre_data = p_preData->getData()[deviceId]->getRawdata();
            uint64_t cur_data = p_data->getData()[deviceId]->getRawdata();
            if (pre_data != std::numeric_limits<uint64_t>::max() && cur_data != std::numeric_limits<uint64_t>::max()) {
                if (pre_data > cur_data) {
                    p_preData = nullptr;
                    return;
                }
            }
        }

        if (measurementData->hasSubdeviceRawData()
        && p_preData->getData().find(deviceId) != p_preData->getData().end()
        && p_preData->getData()[deviceId]->hasSubdeviceRawData()
        && p_data->getData().find(deviceId) != p_data->getData().end()) {
            auto iter_subdevice = measurementData->getSubdeviceRawDatas()->begin();
            while (iter_subdevice != measurementData->getSubdeviceRawDatas()->end() 
            && p_preData->getData()[deviceId]->getSubdeviceRawDatas()->find(iter_subdevice->first) != p_preData->getData()[deviceId]->getSubdeviceRawDatas()->end()) {
                auto &subDeviceId = iter_subdevice->first;
                uint64_t pre_data = p_preData->getData()[deviceId]->getSubdeviceRawData(subDeviceId);
                uint64_t cur_data = p_data->getData()[deviceId]->getSubdeviceRawData(subDeviceId);
                if (pre_data != std::numeric_limits<uint64_t>::max() && cur_data != std::numeric_limits<uint64_t>::max()) {
                    if (pre_data > cur_data) {
                        p_preData->getData()[deviceId]->clearSubdeviceRawdata(subDeviceId);
                    }
                }
                ++iter_subdevice;
            }
        }

        ++iter;
    }
}

void TimeWeightedAverageDataHandler::calculateData(std::shared_ptr<SharedData>& p_data) {
    std::unique_lock<std::mutex> lock(this->mutex);
    if (p_preData == nullptr || p_data == nullptr) {
        return;
    }
    
    std::map<std::string, std::shared_ptr<MeasurementData>>::iterator iter = p_data->getData().begin();
    while (iter != p_data->getData().end()) {
        auto &deviceId = iter->first;
        auto &measurementData = iter->second;
        if (measurementData->hasRawDataOnDevice() 
                && p_preData->getData().find(deviceId) != p_preData->getData().end() 
                && p_preData->getData()[deviceId]->hasRawDataOnDevice()
                && p_data->getData().find(deviceId) != p_data->getData().end()) {
            uint64_t pre_data = p_preData->getData()[deviceId]->getRawdata();
            uint64_t pre_data_raw_timestamp = p_preData->getData()[deviceId]->getRawTimestamp();
            uint64_t cur_data = p_data->getData()[deviceId]->getRawdata();
            uint64_t cur_data_raw_timestamp = p_data->getData()[deviceId]->getRawTimestamp();
            if (pre_data != std::numeric_limits<uint64_t>::max() && cur_data != std::numeric_limits<uint64_t>::max()) {
                if (cur_data_raw_timestamp - pre_data_raw_timestamp != 0) {
                    p_data->getData()[deviceId]->setCurrent((cur_data - pre_data) / (cur_data_raw_timestamp - pre_data_raw_timestamp));
                }
            }
        }

        if (measurementData->hasSubdeviceRawData() 
                && p_preData->getData().find(deviceId) != p_preData->getData().end() 
                && p_preData->getData()[deviceId]->hasSubdeviceRawData() 
                && p_data->getData().find(deviceId) != p_data->getData().end()) {
            std::map<uint32_t, SubdeviceRawData>::const_iterator iter_subdevice = measurementData->getSubdeviceRawDatas()->begin();
            while (iter_subdevice != measurementData->getSubdeviceRawDatas()->end() 
                    && p_preData->getData()[deviceId]->getSubdeviceRawDatas()->find(iter_subdevice->first) != p_preData->getData()[deviceId]->getSubdeviceRawDatas()->end()) {
                auto &subDeviceId = iter_subdevice->first;
                auto &subRawData = iter_subdevice->second;
                uint64_t pre_data = p_preData->getData()[deviceId]->getSubdeviceRawData(subDeviceId);
                uint64_t pre_data_raw_timestamp = p_preData->getData()[deviceId]->getSubdeviceDataRawTimestamp(subDeviceId);
                uint64_t cur_data = subRawData.raw_data;
                uint64_t cur_data_raw_timestamp = subRawData.raw_timestamp;
                if (pre_data != std::numeric_limits<uint64_t>::max() && cur_data != std::numeric_limits<uint64_t>::max()) {
                    if (cur_data_raw_timestamp - pre_data_raw_timestamp != 0) {
                        p_data->getData()[deviceId]->setSubdeviceDataCurrent(subDeviceId, (cur_data - pre_data) / (cur_data_raw_timestamp - pre_data_raw_timestamp));
                    }
                }
                ++iter_subdevice;
            }
        }

        ++iter;
    }
}

void TimeWeightedAverageDataHandler::handleData(std::shared_ptr<SharedData>& p_data) noexcept {
    if (p_preData == nullptr || p_data == nullptr) {
        return;
    }

    counterOverflowDetection(p_data);
    calculateData(p_data);
    updateStatistics(p_data);
}
} // end namespace xpum
