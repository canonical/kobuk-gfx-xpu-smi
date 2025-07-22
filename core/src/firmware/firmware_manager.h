/*
 *  Copyright (C) 2021-2023 Intel Corporation
 *  SPDX-License-Identifier: MIT
 *  @file dump_manager.h
 */

#pragma once

#include <future>
#include <mutex>
#include <string>
#include <vector>

#include "xpum_structs.h"
#include "amc/amc_manager.h"

namespace xpum {

struct AmcCredential {
    std::string username;
    std::string password;
};

namespace gfx_fw_status {
enum GfxFwStatus {
    RESET,
    INIT,
    RECOVERY,
    TEST,
    FW_DISABLED,
    NORMAL,
    DISABLE_WAIT,
    OP_STATE_TRANS,
    INVALID_CPU_PLUGGED_IN,
    UNKNOWN
};
}

struct RunGSCFirmwareFlashParam {
    std::vector<char> img;
    bool force;
    std::string errMsg;
};

struct GetGSCFirmwareFlashResultParam{
    std::string errMsg;
};

class FirmwareManager {
   private:
    std::mutex mtx;

    std::future<xpum_firmware_flash_result_t> taskAMC;
    std::future<xpum_firmware_flash_result_t> taskGSC;
    std::future<xpum_firmware_flash_result_t> taskGSCData;
    std::future<xpum_firmware_flash_result_t> taskLateBinding;

    std::shared_ptr<AmcManager> p_amc_manager;

    void preInitAmcManager();

    bool initAmcManager();

    xpum_result_t atsmHwConfigCompatibleCheck(std::string meiPath, std::vector<char>& buffer);

    xpum_result_t isPVCFwImageAndDeviceCompatible(std::string meiPath, std::vector<char>& buffer);

    xpum_result_t runGscOnlyFwFlash(const char* filePath, bool force);
    void getGscOnlyFwFlashResult(xpum_firmware_flash_task_result_t* result);
    xpum_result_t runGscOnlyFwDataFlash(const char* filePath);
    void getGscOnlyFwDataFlashResult(xpum_firmware_flash_task_result_t* result);
    xpum_result_t runGscOnlyLateBindingFlash(const char* filePath, xpum_firmware_type_t type);
    void getGscOnlyLateBindingFlashResult(xpum_firmware_flash_task_result_t* result, xpum_firmware_type_t type);

    std::string amcFwErrMsg;
    std::string flashFwErrMsg;
    bool isModelSupported(int model) const;

   public:
    void init();
    bool isUpgradingFw(void);

    void detectGscFw();
    xpum_result_t getAMCFirmwareVersions(std::vector<std::string> &versions,AmcCredential credential);
    xpum_result_t runAMCFirmwareFlash(const char* filePath, AmcCredential credential);
    xpum_result_t getAMCFirmwareFlashResult(xpum_firmware_flash_task_result_t *result, AmcCredential credential);
    std::string getAmcWarnMsg();
    xpum_result_t getAMCSensorReading(xpum_sensor_reading_t data[], int *count);

    xpum_result_t runGSCFirmwareFlash(xpum_device_id_t deviceId, const char* filePath, bool force = false, bool igscOnly = false);
    void getGSCFirmwareFlashResult(xpum_device_id_t deviceId, xpum_firmware_flash_task_result_t* result, bool igscOnly = false);

    xpum_result_t runFwDataFlash(xpum_device_id_t deviceId, const char* filePath, bool igscOnly = false);
    void getFwDataFlashResult(xpum_device_id_t deviceId, xpum_firmware_flash_task_result_t* result, bool igscOnly = false);

    xpum_result_t runPscFwFlash(xpum_device_id_t deviceId, const char* filePath, bool force = false);
    void getPscFwFlashResult(xpum_device_id_t deviceId, xpum_firmware_flash_task_result_t* result);

    xpum_result_t runFwCodeDataFlash(xpum_device_id_t deviceId, const char* filePath, int eccState);
    void getFwCodeDataFlashResult(xpum_device_id_t deviceId, xpum_firmware_flash_task_result_t* result);

    xpum_result_t runGSCLateBindingFlash(xpum_device_id_t deviceId, const char* filePath, xpum_firmware_type_t type, bool igscOnly = false);
    void getGSCLateBindingFlashResult(xpum_device_id_t deviceId, xpum_firmware_flash_task_result_t* result, xpum_firmware_type_t type, bool igscOnly = false);

    std::string getAmcFwErrMsg() {
        return amcFwErrMsg;
    }

    std::string getFlashFwErrMsg() {
        return flashFwErrMsg;
    }

    xpum_result_t getAMCSlotSerialNumbers(AmcCredential credential, std::vector<SlotSerialNumberAndFwVersion>& serialNumberList);

    gfx_fw_status::GfxFwStatus getGfxFwStatus(xpum_device_id_t deviceId);

    static std::string transGfxFwStatusToString(gfx_fw_status::GfxFwStatus status);
    
    xpum_result_t getAMCSerialNumbersByRiserSlot(uint8_t riser, uint8_t slot, std::string &serialNumber);

    void credentialCheckIfFail(AmcCredential credential, std::string& errMsg);

    std::mutex mtxPct;
    std::atomic<int> gscFwFlashPercent;
    std::atomic<int> gscFwFlashTotalPercent;
    std::atomic<int> gscFwDataFlashPercent;
    std::atomic<int> gscFwDataFlashTotalPercent;
    std::atomic<int> gscLateBindingFlashPercent;
    std::atomic<int> gscLateBindingFlashTotalPercent;
};

std::vector<char> readImageContent(const char* filePath);

static const std::string igscPath{"igsc"};
} // namespace xpum