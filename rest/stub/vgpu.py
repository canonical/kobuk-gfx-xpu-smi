#
# Copyright (C) 2023-2024 Intel Corporation
# SPDX-License-Identifier: MIT
# @file vgpu.py
#

from .grpc_stub import stub
import core_pb2
from google.protobuf import empty_pb2

DeviceFunctionTypeToString = {
    core_pb2.PHYSICAL: "physical",
    core_pb2.VIRTUAL: "virtual",
    core_pb2.UNKNOWN: "unknown",
}

def doVgpuPrecheck():
    resp = stub.doVgpuPrecheck(empty_pb2.Empty())
    if len(resp.errorMsg) != 0:
        return 1, resp.errorMsg, None
    data = dict()
    data["vmx_flag"] = "Pass" if resp.vmxFlag else "Fail"
    data["vmx_message"] = resp.vmxMessage
    data["iommu_status"] = "Pass" if resp.iommuStatus else "Fail"
    data["iommu_message"] = resp.iommuMessage
    data["sriov_status"] = "Pass" if resp.sriovStatus else "Fail"
    data["sriov_message"] = resp.sriovMessage
    return 0, "OK", data

def createVf(deviceId, numVfs, lmemPerVf):
    resp = stub.createVf(core_pb2.VgpuCreateVfRequest(
        numVfs=numVfs,
        lmemPerVf=lmemPerVf,
        deviceId=deviceId
    ))
    if len(resp.errorMsg) != 0:
        return 1, resp.errorMsg, None
    return 0, "OK", {"result": "OK"}

def listVf(deviceId):
    resp = stub.getDeviceFunction(core_pb2.VgpuGetDeviceFunctionRequest(deviceId=deviceId))
    if len(resp.errorMsg) != 0:
        return 1, resp.errorMsg, None
    data = []
    for f in resp.functionList:
        function = dict()
        function["bdf_address"] = f.bdfAddress
        function["lmem_size"] = f.lmemSize
        function["function_type"] = DeviceFunctionTypeToString[f.deviceFunctionType]
        function["device_id"] = f.deviceId
        data.append(function)
    return 0, "OK", data


def removeAllVf(deviceId):
    resp = stub.removeAllVf(core_pb2.VgpuRemoveAllVfRequest(deviceId=deviceId))
    if len(resp.errorMsg) != 0:
        return 1, resp.errorMsg, None
    return 0, "OK", {"result": "OK"}

def stats(deviceId):
    resp = stub.getVfMetrics(core_pb2.GetVfMetricsRequest(deviceId=deviceId))
    if len(resp.errorMsg) != 0:
        return 1, resp.errorMsg, None
    data = []
    for it in resp.vfList:
        vf = dict()
        vf["bdf_address"] = it.bdfAddress
        metrics = []
        for it1 in it.metricList:
            metric = dict()
            metric["metric_type"] = it1.metricType
            metric["value"] = it1.value
            metric["scale"] = it1.scale
            metrics.append(metric)
        vf["metric_list"] = metrics
        data.append(vf)
    return 0, "OK", data

