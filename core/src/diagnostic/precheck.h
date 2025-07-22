/*
 *  Copyright (C) 2022-2025 Intel Corporation
 *  SPDX-License-Identifier: MIT
 *  @file precheck.h
 */
#pragma once

#include "../include/xpum_structs.h"
#include "infrastructure/configuration.h"
#include <thread>
#include <vector>

namespace xpum {

const int XPUM_MAX_PRECHECK_ERROR_TYPE_INFO_LIST_SIZE = 19;

const int PROCESSOR_COUNT = std::thread::hardware_concurrency();

const xpum_precheck_error_t PRECHECK_ERROR_TYPE_INFO_LIST[XPUM_MAX_PRECHECK_ERROR_TYPE_INFO_LIST_SIZE] = {
    {errorId : 1, errorType : XPUM_GUC_NOT_RUNNING, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 2, errorType : XPUM_GUC_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 3, errorType : XPUM_GUC_INITIALIZATION_FAILED, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 4, errorType : XPUM_IOMMU_CATASTROPHIC_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 5, errorType : XPUM_LMEM_NOT_INITIALIZED_BY_FIRMWARE, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 6, errorType : XPUM_PCIE_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 7, errorType : XPUM_DRM_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 8, errorType : XPUM_GPU_HANG, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 9, errorType : XPUM_I915_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 10, errorType : XPUM_I915_NOT_LOADED, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 11, errorType : XPUM_LEVEL_ZERO_INIT_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 12, errorType : XPUM_HUC_DISABLED, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_HIGH},
    {errorId : 13, errorType : XPUM_HUC_NOT_RUNNING, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_HIGH},
    {errorId : 14, errorType : XPUM_LEVEL_ZERO_METRICS_INIT_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_UMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_HIGH},
    {errorId : 15, errorType : XPUM_MEMORY_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 16, errorType : XPUM_GPU_INITIALIZATION_FAILED, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 17, errorType : XPUM_MEI_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_HIGH},
    {errorId : 18, errorType : XPUM_XE_ERROR, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
    {errorId : 19, errorType : XPUM_XE_NOT_LOADED, errorCategory : XPUM_PRECHECK_ERROR_CATEGORY_KMD, errorSeverity : XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL}};

struct ErrorPattern {
    std::string pattern;
    std::string filter;
    xpum_precheck_component_type_t target_type;
    int error_id;                                       // only for gpu and driver, -1 means cpu
    xpum_precheck_error_category_t error_category;
    xpum_precheck_error_severity_t error_severity;         
};

typedef enum xpum_precheck_log_source {
    XPUM_PRECHECK_LOG_SOURCE_JOURNALCTL = 0,
    XPUM_PRECHECK_LOG_SOURCE_DMESG = 1,
    XPUM_PRECHECK_LOG_SOURCE_FILE = 2
} xpum_precheck_log_source;

const std::vector<ErrorPattern> error_patterns = {
        {".*(GPU HANG).*", "", XPUM_PRECHECK_COMPONENT_TYPE_GPU, XPUM_GPU_HANG},
        {".*(GuC initialization failed).*", "", XPUM_PRECHECK_COMPONENT_TYPE_GPU, XPUM_GUC_INITIALIZATION_FAILED},
        {".*ERROR.*GUC.*", "", XPUM_PRECHECK_COMPONENT_TYPE_GPU, XPUM_GUC_ERROR},
        {".*(IO: IOMMU catastrophic error).*", "", XPUM_PRECHECK_COMPONENT_TYPE_GPU, XPUM_IOMMU_CATASTROPHIC_ERROR},
        {".*(LMEM not initialized by firmware).*", "", XPUM_PRECHECK_COMPONENT_TYPE_GPU, XPUM_LMEM_NOT_INITIALIZED_BY_FIRMWARE},
        {".*(timed out waiting for forcewake ack request).*", "", XPUM_PRECHECK_COMPONENT_TYPE_GPU, XPUM_GPU_INITIALIZATION_FAILED},
    
        // i915/drm error
        {".*i915.*drm.*ERROR.*", "", XPUM_PRECHECK_COMPONENT_TYPE_DRIVER, XPUM_I915_ERROR},
        {".*drm.*ERROR.*", "i915", XPUM_PRECHECK_COMPONENT_TYPE_DRIVER, XPUM_DRM_ERROR},
        // cpu error
        {".*(mce|mca).*err.*", "", XPUM_PRECHECK_COMPONENT_TYPE_CPU, -1,  XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
        {".*caterr.*", "", XPUM_PRECHECK_COMPONENT_TYPE_CPU, -1, XPUM_PRECHECK_ERROR_CATEGORY_HARDWARE, XPUM_PRECHECK_ERROR_SEVERITY_CRITICAL},
        // mei error
        {".*mei_gsc.*(id exceeded).*", "", XPUM_PRECHECK_COMPONENT_TYPE_DRIVER, 17, XPUM_PRECHECK_ERROR_CATEGORY_KMD, XPUM_PRECHECK_ERROR_SEVERITY_HIGH},
        // xe/drm error
        {".*xe.*drm.*ERROR.*", "", XPUM_PRECHECK_COMPONENT_TYPE_DRIVER, XPUM_XE_ERROR},
        {".*drm.*ERROR.*", "xe", XPUM_PRECHECK_COMPONENT_TYPE_DRIVER, XPUM_DRM_ERROR}
};

// The order of the vector impacts how error patterns are matched. It starts from special patterns to general patterns.
const std::vector<std::string> targeted_words = {"hang", "guc", "iommu", "lmem", "forcewake", "mei", "i915", "drm",  "mce", "mca", "caterr"};

class PrecheckManager {
   public:
    static xpum_result_t precheck(xpum_precheck_component_info_t resultList[], int *count, xpum_precheck_options options);

    static xpum_result_t getPrecheckErrorList(xpum_precheck_error_t resultList[], int *count);

    static int cpu_temperature_threshold;

    static std::string KERNEL_MESSAGES_SOURCE;

    static std::string KERNEL_MESSAGES_FILE;

    static xpum_precheck_component_info_t component_driver;

    static std::vector<xpum_precheck_component_info_t> component_cpus;

    static std::vector<xpum_precheck_component_info_t> component_gpus;
};

} // namespace xpum
