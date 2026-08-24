/*++

Copyright (c) 2026 AMD BC-250 Driver Project

Module Name:
    atiadlxx.h

Abstract:
    AMD Display Library (ADL) SDK header definitions for GPU-Z, HWiNFO, and monitoring tools bridge.

--*/

#ifndef _ATIADLXX_H_
#define _ATIADLXX_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define ADL_OK                          0
#define ADL_ERR                         -1
#define ADL_ERR_NOT_SUPPORTED           -8
#define ADL_ERR_INVALID_PARAM           -2

#define ADL_MAX_PATH                    256
#define ADL_MAX_ADAPTERS                150
#define ADL_MAX_DISPLAYS                150
#define ADL_MAX_DEVICENAME              32

#define ADL_ODN_TEMPERATURE_CORE        1
#define ADL_ODN_TEMPERATURE_MEMORY      2
#define ADL_ODN_TEMPERATURE_VRVDDC      3
#define ADL_ODN_TEMPERATURE_HOTSPOT     4

typedef void* ( *ADL_MAIN_MALLOC_CALLBACK )( int );

typedef struct AdapterInfo
{
    int iSize;
    int iAdapterIndex;
    char strUDID[ADL_MAX_PATH];
    int iBusNumber;
    int iDeviceNumber;
    int iFunctionNumber;
    int iVendorID;
    char strAdapterName[ADL_MAX_PATH];
    char strDisplayName[ADL_MAX_PATH];
    int iPresent;
    int iExist;
    char strDriverPath[ADL_MAX_PATH];
    char strDriverPathExt[ADL_MAX_PATH];
    char strPNPString[ADL_MAX_PATH];
    int iOSDisplayIndex;
} AdapterInfo, *LPAdapterInfo;

typedef struct ADLTemperature
{
    int iSize;
    int iTemperature; /* millidegrees Celsius */
} ADLTemperature;

typedef struct ADLPMActivity
{
    int iSize;
    int iEngineClock; /* in 10 KHz */
    int iMemoryClock; /* in 10 KHz */
    int iVddc;        /* in mV */
    int iActivityPercent;
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iReserved;
} ADLPMActivity;

typedef struct ADLOD6CurrentStatus
{
    int iEngineClock; /* in 10 KHz */
    int iMemoryClock; /* in 10 KHz */
    int iActivityPercent;
    int iCurrentPerformanceLevel;
    int iCurrentBusSpeed;
    int iCurrentBusLanes;
    int iMaximumBusLanes;
    int iVddc;        /* in mV */
} ADLOD6CurrentStatus;

typedef struct ADLODNPerformanceLevel
{
    int iClock;       /* in 10 KHz or MHz */
    int iVddc;        /* in mV */
    int iControl;
} ADLODNPerformanceLevel;

typedef struct ADLODNPerformanceLevels
{
    int iSize;
    int iMode;
    int iNumberOfPerformanceLevels;
    ADLODNPerformanceLevel aLevels[8];
} ADLODNPerformanceLevels;

typedef struct ADLOD6PowerControlInfo
{
    int iMinValue;
    int iMaxValue;
    int iStepValue;
    int iDefaultValue;
} ADLOD6PowerControlInfo;

typedef struct ADLMemoryInfo
{
    long long iMemorySize;
    char strMemoryType[ADL_MAX_PATH];
    long long iMemoryBandwidth;
} ADLMemoryInfo, *LPADLMemoryInfo;

typedef struct ADLMemoryInfo2
{
    long long iMemorySize;
    char strMemoryType[ADL_MAX_PATH];
    long long iMemoryBandwidth;
    int iHyperZ;
    int iMemoryType;
    int iMemoryFlags;
} ADLMemoryInfo2, *LPADLMemoryInfo2;

typedef struct ADLASICInfo
{
    int iSize;
    int iASICFamily;
    char strASICName[ADL_MAX_PATH];
    int iRevisionID;
    int iASICRevision;
    int iBusType;
    int iBusSpeed;
    int iBusWidth;
} ADLASICInfo, *LPADLASICInfo;

typedef struct ADLClockInfo
{
    int iCoreClock;   /* in 10 KHz */
    int iMemoryClock; /* in 10 KHz */
} ADLClockInfo, *LPADLClockInfo;

typedef struct ADLODParameterRange
{
    int iMin;
    int iMax;
    int iStep;
} ADLODParameterRange;

typedef struct ADLODParameters
{
    int iSize;
    int iNumberOfPerformanceLevels;
    int iActivityReportingSupported;
    int iDiscretePerformanceLevels;
    int iReserved;
    ADLODParameterRange sEngineClock;
    ADLODParameterRange sMemoryClock;
    ADLODParameterRange sVddc;
} ADLODParameters, *LPADLODParameters;

typedef struct ADLODPerformanceLevel
{
    int iEngineClock; /* in 10 KHz */
    int iMemoryClock; /* in 10 KHz */
    int iVddc;        /* in mV */
} ADLODPerformanceLevel, *LPADLODPerformanceLevel;

typedef struct ADLODPerformanceLevels
{
    int iSize;
    int iReserved;
    ADLODPerformanceLevel aLevels[2];
} ADLODPerformanceLevels, *LPADLODPerformanceLevels;

typedef struct ADLODNParameterRange
{
    int iMode;
    int iMin;
    int iMax;
    int iStep;
    int iDefault;
} ADLODNParameterRange;

typedef struct ADLODNCapabilities
{
    int iSize;
    int iFlags;
    ADLODNParameterRange sEngineClock;
    ADLODNParameterRange sMemoryClock;
    ADLODNParameterRange svddc;
    ADLODNParameterRange sPowerTuneDat;
    ADLODNParameterRange sFanSpeed;
    ADLODNParameterRange sTargetTemperature;
    ADLODNParameterRange sAcousticLimit;
} ADLODNCapabilities;

typedef struct ADLAdapterSpeedCaps
{
    int iSize;
    int iFlags;
    int iCoreSpeed;   /* in 10 KHz */
    int iMemorySpeed; /* in 10 KHz */
    int iCoreSpeedMax;
    int iMemorySpeedMax;
    int iNumberOfCUs;
    int iNumberOfShaders;
    int iNumberOfROPs;
    int iNumberOfTMUs;
} ADLAdapterSpeedCaps;

typedef struct ADLBiosInfo
{
    char strPartNumber[ADL_MAX_PATH];
    char strVersion[ADL_MAX_PATH];
    char strDate[ADL_MAX_PATH];
} ADLBiosInfo;

typedef void* ADL_CONTEXT_HANDLE;

#endif /* _ATIADLXX_H_ */
