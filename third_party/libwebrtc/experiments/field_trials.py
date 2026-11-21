#!/usr/bin/env vpython3

# Copyright (c) 2022 The WebRTC Project Authors. All Rights Reserved.
#
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file in the root of the source
# tree. An additional intellectual property rights grant can be found
# in the file PATENTS.  All contributing project authors may
# be found in the AUTHORS file in the root of the source tree.

import datetime
from datetime import date
import hashlib
import sys
from typing import FrozenSet, List, Set

import argparse
import dataclasses


@dataclasses.dataclass(frozen=True)
class FieldTrial:
    """Representation of all attributes associated with a field trial.

    Attributes:
      key: Field trial key.
      bug: Associated open bug containing more context.
      end_date: Date when the field trial expires and must be deleted.
    """
    key: str
    bug: str
    end_date: date

    def bug_url(self) -> str:
        project, _, bug_id = self.bug.partition(':')
        if not project or not bug_id:
            return ''
        return f'https://crbug.com/{project}/{bug_id}'


# As per the policy in `g3doc/field-trials.md`, all field trials should be
# registered in the container below.
ACTIVE_FIELD_TRIALS: FrozenSet[FieldTrial] = frozenset([
    # keep-sorted start
    FieldTrial('WebRTC-Audio-GainController2',
               'webrtc:7494',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-OpusGeneratePlc',
               'webrtc:13322',
               date(2025, 4, 1)),
    FieldTrial('WebRTC-Audio-PriorityBitrate',
               'webrtc:15769',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AV1-OverridePriorityBitrate',
               'webrtc:15763',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Av1-GetEncoderInfoOverride',
               'webrtc:14931',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DataChannelMessageInterleaving',
               'webrtc:5696',
               date(2024, 10, 1)),
    FieldTrial('WebRTC-DisableRtxRateLimiter',
               'webrtc:15184',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-EncoderDataDumpDirectory',
               'b/296242528',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FrameCadenceAdapter-UseVideoFrameTimestamp',
               'webrtc:15887',
               date(2024, 10, 1)),
    FieldTrial('WebRTC-IPv6NetworkResolutionFixes',
               'webrtc:14334',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IncomingTimestampOnMarkerBitOnly',
               'webrtc:14526',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IncreaseIceCandidatePriorityHostSrflx',
               'webrtc:15020',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-JitterEstimatorConfig',
               'webrtc:14151',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-LibaomAv1Encoder-DisableFrameDropping',
               'webrtc:15225',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-LibaomAv1Encoder-MaxConsecFrameDrop',
               'webrtc:15821',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-LibvpxVp9Encoder-SvcFrameDropConfig',
               'webrtc:15827',
               date(2024, 9, 1)),
    FieldTrial('WebRTC-LibvpxVp8Encoder-AndroidSpecificThreadingSettings',
               'webrtc:15828',
               date(2024, 9, 1)),
    FieldTrial('WebRTC-Pacer-FastRetransmissions',
               'chromium:1354491',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Pacer-KeyframeFlushing',
               'webrtc:11340',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PaddingMode-RecentLargePacket',
               'webrtc:15201',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PermuteTlsClientHello',
               'webrtc:15467',
               date(2024, 7, 1)),
    FieldTrial('WebRTC-PreventSsrcGroupsWithUnexpectedSize',
               'chromium:1459124',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ReceiveBufferSize',
               'webrtc:15585',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RtcEventLogEncodeDependencyDescriptor',
               'webrtc:14975',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RtcEventLogEncodeNetEqSetMinimumDelayKillSwitch',
               'webrtc:14763',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SCM-Timestamp',
               'webrtc:5773',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SendPacketsOnWorkerThread',
               'webrtc:14502',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SetCodecPreferences-ReceiveOnlyFilterInsteadOfThrow',
               'webrtc:15396',
               date(2024, 12, 1)),
    FieldTrial('WebRTC-SrtpRemoveReceiveStream',
               'webrtc:15604',
               date(2024, 10, 1)),
    FieldTrial('WebRTC-TaskQueue-ReplaceLibeventWithStdlib',
               'webrtc:14389',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8-MaxFrameInterval',
               'webrtc:15530',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-EnableRetransmitAllLayers',
               'webrtc:14959',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-EncoderFallbackSettings',
               'webrtc:6634',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-SimulcastIndependentFrameIds',
               'webrtc:15875',
               date(2024, 12, 1)),
    FieldTrial('WebRTC-VideoEncoderSettings',
               'chromium:1406331',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ZeroHertzQueueOverload',
               'webrtc:332381',
               date(2024, 7, 1)),
    FieldTrial('WebRTC-Video-H26xPacketBuffer',
               'webrtc:13485',
               date(2024, 6, 1)),
    # keep-sorted end
])  # yapf: disable

INDEFINITE = date(datetime.MAXYEAR, 1, 1)

# These field trials precedes the policy in `g3doc/field-trials.md` and are
# therefore not required to follow it. Do not add any new field trials here.
# If you remove an entry you should also update
# POLICY_EXEMPT_FIELD_TRIALS_DIGEST.
POLICY_EXEMPT_FIELD_TRIALS: FrozenSet[FieldTrial] = frozenset([
    # keep-sorted start
    FieldTrial('WebRTC-AddNetworkCostToVpn',
               'webrtc:13097',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AddPacingToCongestionWindowPushback',
               'webrtc:10171',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AdjustOpusBandwidth',
               'webrtc:8522',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3AecStateFullResetKillSwitch',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3AecStateSubtractorAnalyzerResetKillSwitch',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3AntiHowlingMinimizationKillSwitch',
               'b/150764764',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ClampInstQualityToOneKillSwitch',
               'webrtc:10913',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ClampInstQualityToZeroKillSwitch',
               'webrtc:10913',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3CoarseFilterResetHangoverKillSwitch',
               'webrtc:12265',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3ConservativeTailFreqResponse',
               'webrtc:13173',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3DeactivateInitialStateResetKillSwitch',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3DelayEstimateSmoothingDelayFoundOverride',
               'webrtc:12775',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3DelayEstimateSmoothingOverride',
               'webrtc:12775',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3EchoSaturationDetectionKillSwitch',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceCaptureDelayEstimationDownmixing',
               'webrtc:11153',
               INDEFINITE),
    FieldTrial(
        'WebRTC-Aec3EnforceCaptureDelayEstimationLeftRightPrioritization',
        'webrtc:11153',
        INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceConservativeHfSuppression',
               'webrtc:11985',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3EnforceLowActiveRenderLimit',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNearendSuppressorHfTuning',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNearendSuppressorTuning',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNormalSuppressorHfTuning',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNormalSuppressorTuning',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceRapidlyAdjustingNearendSuppressorTunings',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceRapidlyAdjustingNormalSuppressorTunings',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceRenderDelayEstimationDownmixing',
               'webrtc:11153',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceSlowlyAdjustingNearendSuppressorTunings',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceSlowlyAdjustingNormalSuppressorTunings',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceStationarityProperties',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceStationarityPropertiesAtInit',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceVeryLowActiveRenderLimit',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3HighPassFilterEchoReference',
               'webrtc:12265',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3MinErleDuringOnsetsKillSwitch',
               'webrtc:10341',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3NonlinearModeReverbKillSwitch',
               'webrtc:11985',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3OnsetDetectionKillSwitch',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial(
        'WebRTC-Aec3RenderDelayEstimationLeftRightPrioritizationKillSwitch',
        'webrtc:11153',
        date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SensitiveDominantNearendActivation',
               'webrtc:11475',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SetupSpecificDefaultConfigDefaultsKillSwitch',
               'webrtc:11151',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ShortHeadroomKillSwitch',
               'webrtc:10341',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3StereoContentDetectionKillSwitch',
               'webrtc:14900',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorAntiHowlingGainOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendEnrExitThresholdOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendEnrThresholdOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendHoldDurationOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendSnrThresholdOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendTriggerThresholdOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendHfMaskSuppressOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendHfMaskTransparentOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendLfMaskSuppressOverride',
               'webrtc:11487',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendLfMaskTransparentOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNearendMaxDecFactorLfOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNearendMaxIncFactorOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalHfMaskSuppressOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalHfMaskTransparentOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalLfMaskSuppressOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalLfMaskTransparentOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalMaxDecFactorLfOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalMaxIncFactorOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorTuningOverride',
               'webrtc:11487',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3TransparentAntiHowlingGain',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3TransparentModeHmm',
               'webrtc:12265',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3TransparentModeKillSwitch',
               'webrtc:9256',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3Use1Dot2SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3Use1Dot6SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3Use2Dot0SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot1SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot2SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot3SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot6SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot9SecondsInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseErleOnsetCompensationInDominantNearend',
               'webrtc:12686',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseLowEarlyReflectionsDefaultGain',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseLowLateReflectionsDefaultGain',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseNearendReverbLen',
               'webrtc:13143',
               INDEFINITE),
    FieldTrial('WebRTC-Aec3UseShortConfigChangeDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseZeroInitialStateDuration',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3VerySensitiveDominantNearendActivation',
               'webrtc:11475',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Agc2SimdAvx2KillSwitch',
               'webrtc:7494',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Agc2SimdNeonKillSwitch',
               'webrtc:7494',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Agc2SimdSse2KillSwitch',
               'webrtc:7494',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AllowMACBasedIPv6',
               'webrtc:12268',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AlrDetectorParameters',
               'webrtc:10542',
               INDEFINITE),
    FieldTrial('WebRTC-AndroidNetworkMonitor-IsAdapterAvailable',
               'webrtc:13741',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ApmExperimentalMultiChannelCaptureKillSwitch',
               'webrtc:14901',
               INDEFINITE),
    FieldTrial('WebRTC-ApmExperimentalMultiChannelRenderKillSwitch',
               'webrtc:14902',
               INDEFINITE),
    FieldTrial('WebRTC-Audio-2ndAgcMinMicLevelExperiment',
               'chromium:1275566',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-ABWENoTWCC',
               'webrtc:8243',
               INDEFINITE),
    FieldTrial('WebRTC-Audio-AdaptivePtime',
               'chromium:1086942',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-Allocation',
               'webrtc:10286',
               INDEFINITE),
    FieldTrial('WebRTC-Audio-AlrProbing',
               'webrtc:10200',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-FecAdaptation',
               'webrtc:8127',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-LegacyOverhead',
               'webrtc:11001',
               INDEFINITE),
    FieldTrial('WebRTC-Audio-MinimizeResamplingOnMobile',
               'webrtc:6181',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-NetEqDecisionLogicConfig',
               'webrtc:13322',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-NetEqDelayManagerConfig',
               'webrtc:10333',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-NetEqNackTrackerConfig',
               'webrtc:10178',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-NetEqSmartFlushing',
               'webrtc:12201',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-OpusAvoidNoisePumpingDuringDtx',
               'webrtc:12380',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-OpusBitrateMultipliers',
               'webrtc:11055',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-OpusPlcUsePrevDecodedSamples',
               'b/143582588',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-Red-For-Opus',
               'webrtc:11640',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-StableTargetAdaptation',
               'webrtc:10981',
               INDEFINITE),
    FieldTrial('WebRTC-Audio-iOS-Holding',
               'webrtc:8126',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AudioDevicePlayoutBufferSizeFactor',
               'webrtc:10928',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AutomaticAnimationDetectionScreenshare',
               'webrtc:11058',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Avx2SupportKillSwitch',
               'webrtc:11663',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BindUsingInterfaceName',
               'webrtc:10707',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BoostedScreenshareQp',
               'webrtc:9659',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-AllocationProbing',
               'webrtc:10394',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-AlrProbing',
               'webrtc:10394',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-EstimateBoundedIncrease',
               'webrtc:10498',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ExponentialProbing',
               'webrtc:10394',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-IgnoreProbesLowerThanNetworkStateEstimate',
               'webrtc:10498',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-InitialProbing',
               'webrtc:10394',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-InjectedCongestionController',
               'webrtc:8415',
               INDEFINITE),
    FieldTrial('WebRTC-Bwe-LimitProbesLowerThanThroughputEstimate',
               'webrtc:11498',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-LinkCapacity',
               'webrtc:9718',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-LossBasedBweV2',
               'webrtc:12707',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-LossBasedControl',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-MaxRttLimit',
               'webrtc:9718',
               INDEFINITE),
    FieldTrial('WebRTC-Bwe-MinAllocAsLowerBound',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-NetworkRouteConstraints',
               'webrtc:11434',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-NoFeedbackReset',
               'webrtc:9718',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-PaceAtMaxOfBweAndLowerLinkCapacity',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ProbingBehavior',
               'webrtc:10394',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ProbingConfiguration',
               'webrtc:10394',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ReceiveTimeFix',
               'webrtc:9054',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ReceiverLimitCapsOnly',
               'webrtc:12306',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-RobustThroughputEstimatorSettings',
               'webrtc:10274',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-SafeResetOnRouteChange',
               'webrtc:9718',
               INDEFINITE),
    FieldTrial('WebRTC-Bwe-SeparateAudioPackets',
               'webrtc:10932',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-TrendlineEstimatorSettings',
               'webrtc:10932',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweBackOffFactor',
               'webrtc:8212',
               INDEFINITE),
    FieldTrial('WebRTC-BweLossExperiment',
               'webrtc:5839',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweRapidRecoveryExperiment',
               'webrtc:8015',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweThroughputWindowConfig',
               'webrtc:10274',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweWindowSizeInPackets',
               'webrtc:8212',
               INDEFINITE),
    FieldTrial('WebRTC-CongestionWindow',
               'webrtc:14898',
               INDEFINITE),
    FieldTrial('WebRTC-CpuLoadEstimator',
               'webrtc:8504',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Debugging-RtpDump',
               'webrtc:10675',
               INDEFINITE),
    FieldTrial('WebRTC-DecoderDataDumpDirectory',
               'webrtc:14236',
               INDEFINITE),
    FieldTrial('WebRTC-DefaultBitrateLimitsKillSwitch',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DependencyDescriptorAdvertised',
               'webrtc:10342',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DisableUlpFecExperiment',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DontIncreaseDelayBasedBweInAlr',
               'webrtc:10542',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DscpFieldTrial',
               'webrtc:13622',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ExtraICEPing',
               'webrtc:10273',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FakeNetworkReceiveConfig',
               'webrtc:14238',
               INDEFINITE),
    FieldTrial('WebRTC-FakeNetworkSendConfig',
               'webrtc:14238',
               INDEFINITE),
    FieldTrial('WebRTC-FilterAbsSendTimeExtension',
               'webrtc:10234',
               INDEFINITE),
    FieldTrial('WebRTC-FindNetworkHandleWithoutIpv6TemporaryPart',
               'webrtc:11067',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FlexFEC-03',
               'webrtc:5654',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FlexFEC-03-Advertised',
               'webrtc:5654',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ForcePlayoutDelay',
               'webrtc:11896',
               INDEFINITE),
    FieldTrial('WebRTC-ForceSendPlayoutDelay',
               'webrtc:11896',
               INDEFINITE),
    FieldTrial('WebRTC-ForceSimulatedOveruseIntervalMs',
               'webrtc:14239',
               INDEFINITE),
    FieldTrial('WebRTC-FrameDropper',
               'webrtc:9711',
               INDEFINITE),
    FieldTrial('WebRTC-FullBandHpfKillSwitch',
               'webrtc:11193',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-GenericCodecDependencyDescriptor',
               'webrtc:14969',
               INDEFINITE),
    FieldTrial('WebRTC-GenericDescriptorAdvertised',
               'webrtc:9361',
               INDEFINITE),
    FieldTrial('WebRTC-GenericDescriptorAuth',
               'webrtc:10103',
               INDEFINITE),
    FieldTrial('WebRTC-GenericPictureId',
               'webrtc:9361',
               INDEFINITE),
    FieldTrial('WebRTC-GetEncoderInfoOverride',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-H264HighProfile',
               'webrtc:6337',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IPv6Default',
               'chromium:413437',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IceControllerFieldTrials',
               'chromium:1024965',
               INDEFINITE),
    FieldTrial('WebRTC-IceFieldTrials',
               'webrtc:11021',
               INDEFINITE),
    FieldTrial('WebRTC-KeyframeInterval',
               'webrtc:10427',
               INDEFINITE),
    FieldTrial('WebRTC-LegacyFrameIdJumpBehavior',
               'webrtc:13343',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-LegacySimulcastLayerLimit',
               'webrtc:8785',
               INDEFINITE),
    FieldTrial('WebRTC-LegacyTlsProtocols',
               'webrtc:10261',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-LowresSimulcastBitrateInterpolation',
               'webrtc:12415',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-MutedStateKillSwitch',
               'b/177830919',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Network-UseNWPathMonitor',
               'webrtc:10966',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-NetworkMonitorAutoDetect',
               'webrtc:13741',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-NormalizeSimulcastResolution',
               '',
               INDEFINITE),
    FieldTrial('WebRTC-Pacer-BlockAudio',
               'webrtc:8415',
               INDEFINITE),
    FieldTrial('WebRTC-Pacer-DrainQueue',
               'webrtc:8415',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Pacer-IgnoreTransportOverhead',
               'webrtc:9883',
               INDEFINITE),
    FieldTrial('WebRTC-Pacer-PadInSilence',
               'webrtc:8415',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PacketBufferMaxSize',
               'webrtc:9851',
               INDEFINITE),
    FieldTrial('WebRTC-PcFactoryDefaultBitrates',
               'webrtc:10865',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PiggybackIceCheckAcknowledgement',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PixelLimitResource',
               'webrtc:12261',
               INDEFINITE),
    FieldTrial('WebRTC-ProbingScreenshareBwe',
               'webrtc:7694',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ProtectionOverheadRateThreshold',
               'webrtc:14899',
               INDEFINITE),
    FieldTrial('WebRTC-QpParsingKillSwitch',
               'webrtc:12542',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RtcEventLogKillSwitch',
               'webrtc:12084',
               INDEFINITE),
    FieldTrial('WebRTC-RtcEventLogNewFormat',
               'webrtc:8111',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RtcpLossNotification',
               'webrtc:10336',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SendBufferSizeBytes',
               'webrtc:11905',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SendNackDelayMs',
               'webrtc:9953',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SetSocketReceiveBuffer',
               'webrtc:13753',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SignalNetworkPreferenceChange',
               'webrtc:11825',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SimulcastEncoderAdapter-GetEncoderInfoOverride',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SimulcastLayerLimitRoundUp',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SpsPpsIdrIsH264Keyframe',
               'webrtc:8423',
               INDEFINITE),
    FieldTrial('WebRTC-StableTargetRate',
               'webrtc:10126',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-StrictPacingAndProbing',
               'webrtc:8072',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-StunInterPacketDelay',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SurfaceCellularTypes',
               'webrtc:11473',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SwitchEncoderOnInitializationFailures',
               'webrtc:13572',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Target-Bitrate-Rtcp',
               'webrtc:9969',
               INDEFINITE),
    FieldTrial('WebRTC-TransientSuppressorForcedOff',
               'chromium:1186705',
               INDEFINITE),
    FieldTrial('WebRTC-UseBaseHeavyVP8TL3RateAllocation',
               'webrtc:9477',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseDifferentiatedCellularCosts',
               'webrtc:11473',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseShortVP8TL2Pattern',
               'webrtc:9477',
               INDEFINITE),
    FieldTrial('WebRTC-UseShortVP8TL3Pattern',
               'webrtc:8162',
               INDEFINITE),
    FieldTrial('WebRTC-UseStandardBytesStats',
               'webrtc:10525',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseTurnServerAsStunServer',
               'webrtc:11059',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8-CpuSpeed-Arm',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8-ForcePartitionResilience',
               'webrtc:11832',
               INDEFINITE),
    FieldTrial('WebRTC-VP8-Forced-Fallback-Encoder-v2',
               'webrtc:6634',
               INDEFINITE),
    FieldTrial('WebRTC-VP8-GetEncoderInfoOverride',
               'webrtc:11832',
               INDEFINITE),
    FieldTrial('WebRTC-VP8-Postproc-Config',
               'webrtc:11551',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8-Postproc-Config-Arm',
               'webrtc:6634',
               INDEFINITE),
    FieldTrial('WebRTC-VP8ConferenceTemporalLayers',
               'webrtc:9260',
               INDEFINITE),
    FieldTrial('WebRTC-VP8IosMaxNumberOfThread',
               'webrtc:10005',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8VariableFramerateScreenshare',
               'webrtc:10310',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-GetEncoderInfoOverride',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-LowTierOptimizations',
               'webrtc:13888',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-PerformanceFlags',
               'webrtc:11551',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9QualityScaler',
               'webrtc:11319',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9VariableFramerateScreenshare',
               'webrtc:10310',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-BalancedDegradation',
               'webrtc:7607',
               INDEFINITE),
    FieldTrial('WebRTC-Video-BalancedDegradationSettings',
               '',
               INDEFINITE),
    FieldTrial('WebRTC-Video-BandwidthQualityScalerSettings',
               'webrtc:12942',
               INDEFINITE),
    FieldTrial('WebRTC-Video-DisableAutomaticResize',
               'webrtc:11812',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-DiscardPacketsWithUnknownSsrc',
               'webrtc:9871',
               INDEFINITE),
    FieldTrial('WebRTC-Video-ForcedSwDecoderFallback',
               '',
               INDEFINITE),
    FieldTrial('WebRTC-Video-InitialDecoderResolution',
               'webrtc:11898',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-MinVideoBitrate',
               'webrtc:10915',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-Pacing',
               'webrtc:10038',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-PreferTemporalSupportOnBaseLayer',
               'webrtc:11324',
               INDEFINITE),
    FieldTrial('WebRTC-Video-QualityRampupSettings',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-QualityScalerSettings',
               '',
               INDEFINITE),
    FieldTrial('WebRTC-Video-QualityScaling',
               'webrtc:9169',
               INDEFINITE),
    FieldTrial('WebRTC-Video-UseFrameRateForOverhead',
               'b/166341943',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-VariableStartScaleFactor',
               '',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VideoFrameTrackingIdAdvertised',
               'webrtc:12594',
               INDEFINITE),
    FieldTrial('WebRTC-VideoLayersAllocationAdvertised',
               'webrtc:1200',
               INDEFINITE),
    FieldTrial('WebRTC-VideoRateControl',
               'webrtc:10223',
               INDEFINITE),
    FieldTrial('WebRTC-VoIPChannelRemixingAdjustmentKillSwitch',
               'chromium:1027117',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Vp9ExternalRefCtrl',
               'webrtc:9585',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Vp9InterLayerPred',
               'chromium:949536',
               INDEFINITE),
    FieldTrial('WebRTC-Vp9IssueKeyFrameOnLayerDeactivation',
               'chromium:889017',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ZeroHertzScreenshare',
               'chromium:1255737',
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ZeroPlayoutDelay',
               'chromium:1335323',
               date(2024, 4, 1)),
    # keep-sorted end
])  # yapf: disable

POLICY_EXEMPT_FIELD_TRIALS_DIGEST: str = \
    '3026f839766eb90355893fa0f1af8e9bf0d0dca1'

REGISTERED_FIELD_TRIALS: FrozenSet[FieldTrial] = ACTIVE_FIELD_TRIALS.union(
    POLICY_EXEMPT_FIELD_TRIALS)


def todays_date() -> date:
    now = datetime.datetime.now(datetime.timezone.utc)
    return date(now.year, now.month, now.day)


def registry_header(
        field_trials: FrozenSet[FieldTrial] = REGISTERED_FIELD_TRIALS) -> str:
    """Generates a C++ header with all field trial keys.

    Args:
      field_trials: Field trials to include in the header.

    Returns:
      String representation of a C++ header file containing all field trial
      keys.

    >>> trials = {
    ...     FieldTrial('B', '', date(1, 1, 1)),
    ...     FieldTrial('A', '', date(1, 1, 1)),
    ...     FieldTrial('B', '', date(2, 2, 2)),
    ... }
    >>> print(registry_header(trials))
    // This file was automatically generated. Do not edit.
    <BLANKLINE>
    #ifndef GEN_REGISTERED_FIELD_TRIALS_H_
    #define GEN_REGISTERED_FIELD_TRIALS_H_
    <BLANKLINE>
    #include "absl/strings/string_view.h"
    <BLANKLINE>
    namespace webrtc {
    <BLANKLINE>
    inline constexpr absl::string_view kRegisteredFieldTrials[] = {
        "A",
        "B",
    };
    <BLANKLINE>
    }  // namespace webrtc
    <BLANKLINE>
    #endif  // GEN_REGISTERED_FIELD_TRIALS_H_
    <BLANKLINE>
    """
    registered_keys = {f.key for f in field_trials}
    keys = '\n'.join(f'    "{k}",' for k in sorted(registered_keys))
    return ('// This file was automatically generated. Do not edit.\n'
            '\n'
            '#ifndef GEN_REGISTERED_FIELD_TRIALS_H_\n'
            '#define GEN_REGISTERED_FIELD_TRIALS_H_\n'
            '\n'
            '#include "absl/strings/string_view.h"\n'
            '\n'
            'namespace webrtc {\n'
            '\n'
            'inline constexpr absl::string_view kRegisteredFieldTrials[] = {\n'
            f'{keys}\n'
            '};\n'
            '\n'
            '}  // namespace webrtc\n'
            '\n'
            '#endif  // GEN_REGISTERED_FIELD_TRIALS_H_\n')


def expired_field_trials(
    threshold: date,
    field_trials: FrozenSet[FieldTrial] = REGISTERED_FIELD_TRIALS
) -> Set[FieldTrial]:
    """Obtains expired field trials.

    Args:
      threshold: Date from which to check end date.
      field_trials: Field trials to validate.

    Returns:
      All expired field trials.

    >>> trials = {
    ...     FieldTrial('Expired', '', date(1, 1, 1)),
    ...     FieldTrial('Not-Expired', '', date(1, 1, 2)),
    ... }
    >>> expired_field_trials(date(1, 1, 1), trials)
    {FieldTrial(key='Expired', bug='', end_date=datetime.date(1, 1, 1))}
    """
    return {f for f in field_trials if f.end_date <= threshold}


def validate_field_trials(
        field_trials: FrozenSet[FieldTrial] = ACTIVE_FIELD_TRIALS
) -> List[str]:
    """Validate that field trials conforms to the policy.

    Args:
      field_trials: Field trials to validate.

    Returns:
      A list of explanations for invalid field trials.
    """
    invalid = []

    sha1 = hashlib.sha1()
    for trial in sorted(POLICY_EXEMPT_FIELD_TRIALS, key=lambda f: f.key):
        sha1.update(trial.key.encode('ascii'))
    if sha1.hexdigest() != POLICY_EXEMPT_FIELD_TRIALS_DIGEST:
        invalid.append(
            'POLICY_EXEMPT_FIELD_TRIALS has been modified. Please note that '
            'you must not add any new entries there. If you removed an entry '
            'you should also update POLICY_EXEMPT_FIELD_TRIALS_DIGEST. The'
            f'new digest is "{sha1.hexdigest()}".')

    for trial in field_trials:
        if not trial.key.startswith('WebRTC-'):
            invalid.append(f'{trial.key} does not start with "WebRTC-".')
        if len(trial.bug) <= 0:
            invalid.append(f'{trial.key} must have an associated bug.')
        if trial.end_date >= INDEFINITE:
            invalid.append(f'{trial.key} must have an end date.')

    return invalid


def cmd_header(args: argparse.Namespace) -> None:
    if not args.no_validation:
        if errors := validate_field_trials():
            print('\n'.join(sorted(errors)))
            sys.exit(1)

    args.output.write(registry_header())


def cmd_expired(args: argparse.Namespace) -> None:
    today = todays_date()
    diff = datetime.timedelta(days=args.in_days)
    expired = expired_field_trials(
        today + diff,
        ACTIVE_FIELD_TRIALS if args.no_exempt else REGISTERED_FIELD_TRIALS)

    if len(expired) <= 0:
        return

    expired_by_date = sorted(expired, key=lambda f: (f.end_date, f.key))
    print('\n'.join(
        f'{f.key} '
        f'{f"<{f.bug_url()}> " if f.bug_url() else ""}'
        f'{"expired" if f.end_date <= today else "expires"} on {f.end_date}'
        for f in expired_by_date))
    if any(f.end_date <= today for f in expired_by_date):
        sys.exit(1)


def cmd_validate(args: argparse.Namespace) -> None:
    del args
    invalid = validate_field_trials()

    if len(invalid) <= 0:
        return

    print('\n'.join(sorted(invalid)))
    sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser()
    subcommand = parser.add_subparsers(dest='cmd')

    parser_header = subcommand.add_parser(
        'header',
        help='generate C++ header file containing registered field trial keys')
    parser_header.add_argument('--output',
                               default=sys.stdout,
                               type=argparse.FileType('w'),
                               required=False,
                               help='output file')
    parser_header.add_argument(
        '--no-validation',
        default=False,
        action='store_true',
        required=False,
        help='whether to validate the field trials before writing')
    parser_header.set_defaults(cmd=cmd_header)

    parser_expired = subcommand.add_parser(
        'expired',
        help='lists all expired field trials',
        description='''
        Lists all expired field trials. Exits with a non-zero exit status if
        any field trials has expired, ignoring the --in-days argument.
        ''')
    parser_expired.add_argument(
        '--no-exempt',
        default=False,
        action='store_true',
        required=False,
        help='whether to include policy exempt field trials')
    parser_expired.add_argument(
        '--in-days',
        default=0,
        type=int,
        required=False,
        help='number of days relative to today to check')
    parser_expired.set_defaults(cmd=cmd_expired)

    parser_validate = subcommand.add_parser(
        'validate',
        help='validates that all field trials conforms to the policy.',
        description='''
        Validates that all field trials conforms to the policy. Exits with a
        non-zero exit status if any field trials does not.
        ''')
    parser_validate.set_defaults(cmd=cmd_validate)

    args = parser.parse_args()

    if not args.cmd:
        parser.print_help(sys.stderr)
        sys.exit(1)

    args.cmd(args)


if __name__ == '__main__':
    main()
