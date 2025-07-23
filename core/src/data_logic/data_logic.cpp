/* 
 *  Copyright (C) 2021-2024 Intel Corporation
 *  SPDX-License-Identifier: MIT
 *  @file data_logic.cpp
 */

#include "data_logic.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

#include "core/core.h"
#include "api/device_model.h"
#include "db_persistency.h"
#include "engine_measurement_data.h"
#include "fabric_measurement_data.h"
#include "infrastructure/configuration.h"
#include "infrastructure/const.h"
#include "infrastructure/exception/ilegal_state_exception.h"
#include "infrastructure/logger.h"
#include "infrastructure/utility.h"
#include "device/gpu/gpu_device_stub.h"

namespace xpum {

DataLogic::DataLogic() : p_data_handler_manager(nullptr),
                         p_persistency(nullptr) {
    XPUM_LOG_TRACE("DataLogic()");
}

DataLogic::~DataLogic() {
    XPUM_LOG_TRACE("~DataLogic()");
}

void DataLogic::init() {
    p_persistency = std::make_shared<DBPersistency>();
    p_data_handler_manager = std::make_unique<DataHandlerManager>(p_persistency);
    p_data_handler_manager->init();
}

void DataLogic::close() {
    if (p_data_handler_manager != nullptr) {
        p_data_handler_manager->close();
    }
}

void DataLogic::storeMeasurementData(MeasurementType type, Timestamp_t time,
                                     std::shared_ptr<std::map<std::string, std::shared_ptr<MeasurementData>>> datas) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    p_data_handler_manager->storeMeasurementData(type, time, datas);
}

std::shared_ptr<MeasurementData> DataLogic::getLatestData(MeasurementType type,
                                                          std::string& device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    return p_data_handler_manager->getLatestData(type, device_id);
}

std::shared_ptr<MeasurementData> DataLogic::getLatestStatistics(MeasurementType type, std::string& device_id, uint64_t session_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    return p_data_handler_manager->getLatestStatistics(type, device_id, session_id);
}

xpum_result_t DataLogic::getMetricsStatistics(xpum_device_id_t deviceId,
                                              xpum_device_stats_t dataList[],
                                              uint32_t* count,
                                              uint64_t* begin,
                                              uint64_t* end,
                                              uint64_t session_id) {
    if (Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId)) == nullptr) {
        return XPUM_RESULT_DEVICE_NOT_FOUND;
    }
    Property prop;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getProperty(XPUM_DEVICE_PROPERTY_INTERNAL_NUMBER_OF_SUBDEVICE, prop);
    uint32_t num_subdevice = prop.getValueInt();
    if (dataList == nullptr) {
        *count = num_subdevice + 1;
        return XPUM_OK;
    }

    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getProperty(XPUM_DEVICE_PROPERTY_INTERNAL_PCI_BDF_ADDRESS, prop);
    std::string bdf = prop.getValue();

    std::map<MeasurementType, std::shared_ptr<MeasurementData>> m_datas;
    auto metric_types = Configuration::getEnabledMetrics();
    std::vector<xpum::DeviceCapability> capabilities;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getCapability(capabilities);
    for (auto metric = metric_types.begin(); metric != metric_types.end();) {
        if (std::none_of(capabilities.begin(), capabilities.end(), [metric](xpum::DeviceCapability cap) { return (cap == Utility::capabilityFromMeasurementType(*metric)); })) {
            metric = metric_types.erase(metric);
        } else {
            metric++;
        }
    }

    auto metric_types_iter = metric_types.begin();
    bool hasDataOnDevice = false;
    std::string device_id = std::to_string(deviceId);
    while (metric_types_iter != metric_types.end()) {
        if (*metric_types_iter != METRIC_ENGINE_UTILIZATION && *metric_types_iter != METRIC_FABRIC_THROUGHPUT) {
            std::shared_ptr<MeasurementData> p_data = std::make_shared<MeasurementData>();
            auto p_pvc_idle_power = GPUDeviceStub::loadPVCIdlePowers(bdf, false);
            if (*metric_types_iter == METRIC_POWER && p_pvc_idle_power->hasDataOnDevice()) {
                p_data = p_pvc_idle_power;
            } else {
                p_data = getLatestStatistics(*metric_types_iter, device_id, session_id);
            }
            if (p_data != nullptr) {
                hasDataOnDevice = hasDataOnDevice || p_data->hasDataOnDevice();
                m_datas.insert(std::make_pair(*metric_types_iter, p_data));
            } else if ((*metric_types_iter >= METRIC_RAS_ERROR_CAT_RESET && *metric_types_iter <= METRIC_RAS_ERROR_CAT_NON_COMPUTE_ERRORS_UNCORRECTABLE)
                    || (*metric_types_iter >= METRIC_EU_ACTIVE && *metric_types_iter <= METRIC_EU_IDLE)) {
                auto start_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                auto end_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                while (end_time - start_time <= 30) {
                    p_data = getLatestStatistics(*metric_types_iter, device_id, session_id);
                    if (p_data != nullptr) {
                        hasDataOnDevice = hasDataOnDevice || p_data->hasDataOnDevice();
                        m_datas.insert(std::make_pair(*metric_types_iter, p_data));
                        break; 
                    }
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    end_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                }
            }
        }
        ++metric_types_iter;
    }
    *begin = getStatsTimestamp(session_id, deviceId);
    *end = Utility::getCurrentTime();

    std::map<MeasurementType, std::shared_ptr<MeasurementData>>::iterator datas_iter = m_datas.begin();
    xpum_device_stats_t device_stats{};
    device_stats.deviceId = deviceId;
    device_stats.isTileData = false;
    device_stats.count = 0;
    if (hasDataOnDevice) {
        while (datas_iter != m_datas.end()) {
            auto &measurementData = datas_iter->second;
            if (measurementData->hasDataOnDevice()) {
                xpum_device_stats_data_t stats_data{};
                MeasurementType type = datas_iter->first;
                stats_data.metricsType = Utility::xpumStatsTypeFromMeasurementType(type);
                stats_data.scale = measurementData->getScale();
                if (Utility::isCounterMetric(type)) {
                    stats_data.isCounter = true;
                    stats_data.accumulated = measurementData->getCurrent();
                    stats_data.value = measurementData->getCurrent() - measurementData->getMin();
                } else {
                    stats_data.isCounter = false;
                    stats_data.avg = measurementData->getAvg();
                    stats_data.min = measurementData->getMin();
                    stats_data.max = measurementData->getMax();
                    stats_data.value = measurementData->getCurrent();
                }
                device_stats.dataList[device_stats.count++] = stats_data;
            }
            ++datas_iter;
        }
    }
    uint32_t index = 0;
    if (index >= *count) {
        return XPUM_BUFFER_TOO_SMALL;
    }
    dataList[index++] = device_stats;

    for (uint32_t i = 0; i < num_subdevice; i++) {
        xpum_device_stats_t subdevice_stats;
        subdevice_stats.deviceId = deviceId;
        subdevice_stats.tileId = i;
        subdevice_stats.isTileData = true;
        subdevice_stats.count = 0;
        datas_iter = m_datas.begin();
        while (datas_iter != m_datas.end()) {
            auto &measurementData = datas_iter->second;
            if (measurementData->hasSubdeviceData() && measurementData->getSubdeviceDatas()->find(i) != measurementData->getSubdeviceDatas()->end() && measurementData->getSubdeviceDataCurrent(i) != std::numeric_limits<uint64_t>::max()) {
                xpum_device_stats_data_t stats_data{};
                MeasurementType type = datas_iter->first;
                stats_data.metricsType = Utility::xpumStatsTypeFromMeasurementType(type);
                stats_data.scale = measurementData->getScale();
                if (Utility::isCounterMetric(type)) {
                    stats_data.isCounter = true;
                    stats_data.accumulated = measurementData->getSubdeviceDataCurrent(i);
                    stats_data.value = measurementData->getSubdeviceDataCurrent(i) - measurementData->getSubdeviceDataMin(i);
                } else {
                    stats_data.isCounter = false;
                    stats_data.avg = measurementData->getSubdeviceDataAvg(i);
                    stats_data.min = measurementData->getSubdeviceDataMin(i);
                    stats_data.max = measurementData->getSubdeviceDataMax(i);
                    stats_data.value = measurementData->getSubdeviceDataCurrent(i);
                }
                subdevice_stats.dataList[subdevice_stats.count++] = stats_data;
            }
            ++datas_iter;
        }
        if (index >= *count) {
            return XPUM_BUFFER_TOO_SMALL;
        }
        dataList[index++] = subdevice_stats;
    }
    *count = index;
    return XPUM_OK;
}

void DataLogic::getLatestMetrics(xpum_device_id_t deviceId,
                                 xpum_device_metrics_t dataList[],
                                 int* count) {
    if (Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId)) == nullptr) {
        return;
    }
    Property prop;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getProperty(XPUM_DEVICE_PROPERTY_INTERNAL_NUMBER_OF_SUBDEVICE, prop);
    uint32_t num_subdevice = prop.getValueInt();
    *count = num_subdevice + 1;
    if (dataList == nullptr) {
        return;
    }

    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getProperty(XPUM_DEVICE_PROPERTY_INTERNAL_PCI_BDF_ADDRESS, prop);
    std::string bdf = prop.getValue();

    std::map<MeasurementType, std::shared_ptr<MeasurementData>> m_datas;
    auto metric_types = Configuration::getEnabledMetrics();
    std::vector<xpum::DeviceCapability> capabilities;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getCapability(capabilities);
    for (auto metric = metric_types.begin(); metric != metric_types.end();) {
        if (std::none_of(capabilities.begin(), capabilities.end(), [metric](xpum::DeviceCapability cap) { return (cap == Utility::capabilityFromMeasurementType(*metric)); })) {
            metric = metric_types.erase(metric);
        } else {
            metric++;
        }
    }

    auto metric_types_iter = metric_types.begin();
    bool hasDataOnDevice = false;
    std::string device_id = std::to_string(deviceId);
    while (metric_types_iter != metric_types.end()) {
        if (*metric_types_iter != METRIC_ENGINE_UTILIZATION && *metric_types_iter != METRIC_FABRIC_THROUGHPUT) {
            std::shared_ptr<MeasurementData> m_data = std::make_shared<MeasurementData>();
            auto p_pvc_idle_power = GPUDeviceStub::loadPVCIdlePowers(bdf, false);
            if (*metric_types_iter == METRIC_POWER && p_pvc_idle_power->hasDataOnDevice(), false) {
                m_data = p_pvc_idle_power;
            } else {
                m_data = getLatestData(*metric_types_iter, device_id);
            }
            if (m_data != nullptr) {
                hasDataOnDevice = hasDataOnDevice || m_data->hasDataOnDevice();
                m_datas.insert(std::make_pair(*metric_types_iter, m_data));
            }
        }
        ++metric_types_iter;
    }

    std::map<MeasurementType, std::shared_ptr<MeasurementData>>::iterator datas_iter = m_datas.begin();
    xpum_device_metrics_t device_metrics{};
    device_metrics.deviceId = deviceId;
    device_metrics.isTileData = false;
    device_metrics.count = 0;
    if (hasDataOnDevice) {
        while (datas_iter != m_datas.end()) {
            auto &measurementData = datas_iter->second;
            if (measurementData->hasDataOnDevice()) {
                xpum_device_metric_data_t metric_data;
                MeasurementType type = datas_iter->first;
                metric_data.metricsType = Utility::xpumStatsTypeFromMeasurementType(type);
                metric_data.isCounter = Utility::isCounterMetric(type) ? true : false;
                metric_data.value = measurementData->getCurrent();
                metric_data.timestamp = measurementData->getTimestamp();
                metric_data.scale = measurementData->getScale();
                device_metrics.dataList[device_metrics.count++] = metric_data;
            }
            ++datas_iter;
        }
    }
    int index = 0;
    dataList[index++] = device_metrics;

    for (uint32_t i = 0; i < num_subdevice; i++) {
        xpum_device_metrics_t subdevice_metrics;
        subdevice_metrics.deviceId = deviceId;
        subdevice_metrics.tileId = i;
        subdevice_metrics.isTileData = true;
        subdevice_metrics.count = 0;
        datas_iter = m_datas.begin();
        while (datas_iter != m_datas.end()) {
            auto &measurementData = datas_iter->second;
            if (measurementData->hasSubdeviceData() && measurementData->getSubdeviceDatas()->find(i) != measurementData->getSubdeviceDatas()->end() && measurementData->getSubdeviceDataCurrent(i) != std::numeric_limits<uint64_t>::max()) {
                xpum_device_metric_data_t metric_data;
                MeasurementType type = datas_iter->first;
                metric_data.metricsType = Utility::xpumStatsTypeFromMeasurementType(type);
                metric_data.isCounter = Utility::isCounterMetric(type) ? true : false;
                metric_data.value = measurementData->getSubdeviceDataCurrent(i);
                metric_data.timestamp = measurementData->getTimestamp();
                metric_data.scale = measurementData->getScale();
                subdevice_metrics.dataList[subdevice_metrics.count++] = metric_data;
            }
            ++datas_iter;
        }
        dataList[index++] = subdevice_metrics;
    }
}

xpum_result_t DataLogic::getEngineStatistics(xpum_device_id_t deviceId,
                                             xpum_device_engine_stats_t dataList[],
                                             uint32_t* count,
                                             uint64_t* begin,
                                             uint64_t* end,
                                             uint64_t session_id) {
    std::string device_id = std::to_string(deviceId);
    if (Core::instance().getDeviceManager()->getDevice(device_id) == nullptr) {
        return XPUM_RESULT_DEVICE_NOT_FOUND;
    }

    uint32_t engine_count = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getEngineCount();
    if (dataList == nullptr) {
        *count = engine_count;
        return XPUM_OK;
    }

    *begin = getEngineStatsTimestamp(session_id, deviceId);
    *end = Utility::getCurrentTime();
    std::map<MeasurementType, std::shared_ptr<MeasurementData>> m_datas;
    auto metric_types = Configuration::getEnabledMetrics();
    if (metric_types.find(METRIC_ENGINE_UTILIZATION) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_ENABLED;
    }
    std::vector<xpum::DeviceCapability> capabilities;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getCapability(capabilities);
    for (auto metric = metric_types.begin(); metric != metric_types.end();) {
        if (std::none_of(capabilities.begin(), capabilities.end(), [metric](xpum::DeviceCapability cap) { return (cap == Utility::capabilityFromMeasurementType(*metric)); })) {
            metric = metric_types.erase(metric);
        } else {
            metric++;
        }
    }
    if (metric_types.find(METRIC_ENGINE_UTILIZATION) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_SUPPORTED;
    }

    std::shared_ptr<MeasurementData> p_data = getLatestStatistics(METRIC_ENGINE_UTILIZATION, device_id, session_id);
    if (p_data == nullptr) {
        *count = 0;
        return XPUM_OK;
    }
    auto iter = p_data->getMultiMetricsDatas()->begin();
    uint32_t index = 0;
    while (iter != p_data->getMultiMetricsDatas()->end()) {
        auto &engineHandle = iter->first;
        auto &measurementData = iter->second;
        uint32_t engine_index = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getEngineIndex(engineHandle);
        if (engine_index != std::numeric_limits<uint32_t>::max() && measurementData.current != std::numeric_limits<uint64_t>::max()) {
            xpum_device_engine_stats_t data;
            data.isTileData = measurementData.on_subdevice;
            data.tileId = measurementData.subdevice_id;
            data.value = measurementData.current;
            data.min = measurementData.min;
            data.avg = measurementData.avg;
            data.max = measurementData.max;
            data.index = engine_index;
            data.scale = p_data->getScale();
            data.type = Utility::toXPUMEngineType(std::static_pointer_cast<EngineCollectionMeasurementData>(p_data)->getEngineType(engineHandle));
            data.deviceId = deviceId;
            if (index >= *count) {
                return XPUM_BUFFER_TOO_SMALL;
            }
            dataList[index] = data;
            ++index;
        }
        ++iter;
    }
    *count = index;
    return XPUM_OK;
}

xpum_result_t DataLogic::getEngineUtilizations(xpum_device_id_t deviceId,
                                               xpum_device_engine_metric_t dataList[],
                                               uint32_t* count) {
    std::string device_id = std::to_string(deviceId);
    if (Core::instance().getDeviceManager()->getDevice(device_id) == nullptr) {
        *count = 0;
        return XPUM_RESULT_DEVICE_NOT_FOUND;
    }

    std::map<MeasurementType, std::shared_ptr<MeasurementData>> m_datas;
    auto metric_types = Configuration::getEnabledMetrics();
    if (metric_types.find(METRIC_ENGINE_UTILIZATION) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_ENABLED;
    }
    std::vector<xpum::DeviceCapability> capabilities;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getCapability(capabilities);
    for (auto metric = metric_types.begin(); metric != metric_types.end();) {
        if (std::none_of(capabilities.begin(), capabilities.end(), [metric](xpum::DeviceCapability cap) { return (cap == Utility::capabilityFromMeasurementType(*metric)); })) {
            metric = metric_types.erase(metric);
        } else {
            metric++;
        }
    }
    if (metric_types.find(METRIC_ENGINE_UTILIZATION) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_SUPPORTED;
    }

    *count = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getEngineCount();
    if (dataList == nullptr) {
        return XPUM_OK;
    }

    std::shared_ptr<MeasurementData> p_data = getLatestData(METRIC_ENGINE_UTILIZATION, device_id);
    if (p_data == nullptr) {
        *count = 0;
        return XPUM_OK;
    }
    auto iter = p_data->getMultiMetricsDatas()->begin();
    uint32_t index = 0;
    while (iter != p_data->getMultiMetricsDatas()->end()) {
        auto &engineHandle = iter->first;
        auto &measurementData = iter->second;
        uint32_t engine_index = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getEngineIndex(engineHandle);
        if (engine_index != std::numeric_limits<uint32_t>::max()) {
            xpum_device_engine_metric_t data;
            data.isTileData = measurementData.on_subdevice;
            data.tileId = measurementData.subdevice_id;
            data.value = measurementData.current;
            data.index = engine_index;
            data.scale = p_data->getScale();
            data.type = Utility::toXPUMEngineType(std::static_pointer_cast<EngineCollectionMeasurementData>(p_data)->getEngineType(engineHandle));
            if (index >= *count) {
                return XPUM_BUFFER_TOO_SMALL;
            }
            dataList[index] = data;
            ++index;
        }
        ++iter;
    }
    *count = index;
    return XPUM_OK;
}

xpum_result_t DataLogic::getFabricThroughputStatistics(xpum_device_id_t deviceId,
                                                       xpum_device_fabric_throughput_stats_t dataList[],
                                                       uint32_t* count,
                                                       uint64_t* begin,
                                                       uint64_t* end,
                                                       uint64_t session_id) {
    std::string device_id = std::to_string(deviceId);
    if (Core::instance().getDeviceManager()->getDevice(device_id) == nullptr) {
        *count = 0;
        return XPUM_RESULT_DEVICE_NOT_FOUND;
    }

    auto metric_types = Configuration::getEnabledMetrics();
    if (metric_types.find(METRIC_FABRIC_THROUGHPUT) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_ENABLED;
    }
    std::vector<xpum::DeviceCapability> capabilities;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getCapability(capabilities);
    for (auto metric = metric_types.begin(); metric != metric_types.end();) {
        if (std::none_of(capabilities.begin(), capabilities.end(), [metric](xpum::DeviceCapability cap) { return (cap == Utility::capabilityFromMeasurementType(*metric)); })) {
            metric = metric_types.erase(metric);
        } else {
            metric++;
        }
    }
    if (metric_types.find(METRIC_FABRIC_THROUGHPUT) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_SUPPORTED;
    }

    uint32_t throughput_count = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getFabricThroughputInfoCount();
    if (dataList == nullptr || throughput_count == 0) {
        *count = throughput_count;
        return XPUM_OK;
    }

    uint32_t total = 0;
    std::shared_ptr<MeasurementData> p_data = getLatestStatistics(METRIC_FABRIC_THROUGHPUT, device_id, session_id);

    if(p_data == nullptr){
        *count = 0;
        return XPUM_OK;
    }

    auto fabric_datas_iter = std::static_pointer_cast<FabricMeasurementData>(p_data)->getMultiMetricsDatas()->begin();
    while (fabric_datas_iter != std::static_pointer_cast<FabricMeasurementData>(p_data)->getMultiMetricsDatas()->end()) {
        FabricThroughputInfo info;
        if (Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getFabricThroughputInfo(fabric_datas_iter->first, info)) {
            ++total;
        }
        ++fabric_datas_iter;
    }
    if(total > *count){
        *count = total;
        return XPUM_BUFFER_TOO_SMALL;
    }

    uint32_t index = 0;
    *begin = getFabricStatsTimestamp(session_id, deviceId);
    *end = Utility::getCurrentTime();
    if (p_data->getTimestamp() < *begin) {
        *count = 0;
        return XPUM_OK;
    }

    auto iter = p_data->getMultiMetricsDatas()->begin();
    while (iter != p_data->getMultiMetricsDatas()->end()) {
        auto &fabricId = iter->first;
        auto &measurementData = iter->second;
        FabricThroughputInfo info;
        if (Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getFabricThroughputInfo(fabricId, info)) {
            xpum_device_fabric_throughput_stats_t stats{};
            stats.tile_id = info.attach_id;
            std::string did = 
                Core::instance().getDeviceManager()->getDeviceIDByFabricID(
                        info.remote_fabric_id);
            if (did.empty() == true) {
                return XPUM_GENERIC_ERROR;
            } else {
                stats.remote_device_id = std::stoi(did);
            }
            stats.remote_device_tile_id = info.remote_attach_id;
            stats.type = Utility::toXPUMFabricThroughputType(info.type);
            if (info.type == TRANSMITTED_COUNTER || info.type == RECEIVED_COUNTER) {
                stats.value = measurementData.current - measurementData.min;
                stats.accumulated = measurementData.current;
                stats.scale = 1;
            } else {
                stats.value = measurementData.current;
                stats.min = measurementData.min;
                stats.avg = measurementData.avg;
                stats.max = measurementData.max;
                stats.scale = p_data->getScale();
            }
            stats.deviceId = deviceId;
            dataList[index] = stats;
            ++index;
        }
        ++iter;
    }
    *count = index;
    return XPUM_OK;
}

xpum_result_t DataLogic::getFabricThroughput(xpum_device_id_t deviceId,
                                             xpum_device_fabric_throughput_metric_t dataList[],
                                             uint32_t* count) {
    std::string device_id = std::to_string(deviceId);
    if (Core::instance().getDeviceManager()->getDevice(device_id) == nullptr) {
        *count = 0;
        return XPUM_RESULT_DEVICE_NOT_FOUND;
    }

    auto metric_types = Configuration::getEnabledMetrics();
    if (metric_types.find(METRIC_FABRIC_THROUGHPUT) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_ENABLED;
    }
    std::vector<xpum::DeviceCapability> capabilities;
    Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getCapability(capabilities);
    for (auto metric = metric_types.begin(); metric != metric_types.end();) {
        if (std::none_of(capabilities.begin(), capabilities.end(), [metric](xpum::DeviceCapability cap) { return (cap == Utility::capabilityFromMeasurementType(*metric)); })) {
            metric = metric_types.erase(metric);
        } else {
            metric++;
        }
    }
    if (metric_types.find(METRIC_FABRIC_THROUGHPUT) == metric_types.end()) {
        *count = 0;
        return XPUM_METRIC_NOT_SUPPORTED;
    }

    uint32_t throughput_count = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getFabricThroughputInfoCount();
    if (dataList == nullptr || *count == 0) {
        *count = throughput_count;
        return XPUM_OK;
    }

    uint32_t index = 0;
    std::shared_ptr<MeasurementData> p_data = getLatestData(METRIC_FABRIC_THROUGHPUT, device_id);

    if(p_data == nullptr){
        *count = 0;
        return XPUM_OK;
    }

    auto iter = p_data->getMultiMetricsDatas()->begin();
    while (iter != p_data->getMultiMetricsDatas()->end()) {
        auto &fabricId = iter->first;
        auto &measurementData = iter->second;
        FabricThroughputInfo info;
        if (Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getFabricThroughputInfo(fabricId, info)) {
            xpum_device_fabric_throughput_metric_t stats;
            stats.tile_id = info.attach_id;
            std::string did = 
                Core::instance().getDeviceManager()->getDeviceIDByFabricID(
                        info.remote_fabric_id);
            if (did.empty() == true) {
                return XPUM_GENERIC_ERROR;
            } else {
                stats.remote_device_id = std::stoi(did);
            }
            stats.remote_device_tile_id = info.remote_attach_id;
            stats.type = Utility::toXPUMFabricThroughputType(info.type);
            if (info.type == TRANSMITTED_COUNTER || info.type == RECEIVED_COUNTER) {
                stats.scale = 1;
            } else {
                stats.scale = p_data->getScale();
            }
            stats.value = measurementData.current;
            if (index >= *count) {
                return XPUM_BUFFER_TOO_SMALL;
            }
            dataList[index] = stats;
            ++index;
        }
        ++iter;
    }
    *count = index;
    return XPUM_OK;
}

bool DataLogic::getFabricLinkInfo(xpum_device_id_t deviceId,
                                  FabricLinkInfo info[],
                                  uint32_t* count) {
    std::string device_id = std::to_string(deviceId);
    if (Core::instance().getDeviceManager()->getDevice(device_id) == nullptr) {
        return false;
    }

    uint32_t index = 0;
    auto fabric_throughput_info = Core::instance().getDeviceManager()->getDevice(std::to_string(deviceId))->getFabricThroughputIDS();
    auto attach_iter = fabric_throughput_info.begin();
    while (attach_iter != fabric_throughput_info.end()) {
        auto remote_fabric_iter = attach_iter->second.begin();
        while (remote_fabric_iter != attach_iter->second.end()) {
            auto remote_attach_iter = remote_fabric_iter->second.begin();
            while (remote_attach_iter != remote_fabric_iter->second.end()) {
                if (info != nullptr) {
                    FabricLinkInfo link;
                    link.tile_id = attach_iter->first;
                    std::string did = Core::instance().getDeviceManager(
                            )->getDeviceIDByFabricID(remote_fabric_iter->first);
                    if (did.empty() == true) {
                        return false;
                    } else {
                        link.remote_device_id = std::stoi(did);
                    }
                    link.remote_tile_id = remote_attach_iter->first;
                    info[index] = link;
                }
                ++index;
                ++remote_attach_iter;
            }
            ++remote_fabric_iter;
        }
        ++attach_iter;
    }
    *count = index;
    return true;
}

void DataLogic::updateStatsTimestamp(uint32_t session_id, uint32_t device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    p_data_handler_manager->updateStatsTimestamp(session_id, device_id);
}

uint64_t DataLogic::getStatsTimestamp(uint32_t session_id, uint32_t device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    return p_data_handler_manager->getStatsTimestamp(session_id, device_id);
}

void DataLogic::updateEngineStatsTimestamp(uint32_t session_id, uint32_t device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    p_data_handler_manager->updateEngineStatsTimestamp(session_id, device_id);
}

uint64_t DataLogic::getEngineStatsTimestamp(uint32_t session_id, uint32_t device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    return p_data_handler_manager->getEngineStatsTimestamp(session_id, device_id);
}

void DataLogic::updateFabricStatsTimestamp(uint32_t session_id, uint32_t device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    p_data_handler_manager->updateFabricStatsTimestamp(session_id, device_id);
}

uint64_t DataLogic::getFabricStatsTimestamp(uint32_t session_id, uint32_t device_id) {
    if (p_data_handler_manager == nullptr) {
        throw IlegalStateException("initialization is not done!");
    }
    return p_data_handler_manager->getFabricStatsTimestamp(session_id, device_id);
}

} // end namespace xpum
