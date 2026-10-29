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
      bug_id: Associated open bug containing more context.
      end_date: Date when the field trial expires and must be deleted.
    """
    key: str
    bug_id: int
    end_date: date

    def bug_url(self) -> str:
        if self.bug_id <= 0:
            return ''
        return f'https://issues.webrtc.org/issues/{self.bug_id}'


# As per the policy in `g3doc/field-trials.md`, all field trials should be
# registered in the container below.
ACTIVE_FIELD_TRIALS: FrozenSet[FieldTrial] = frozenset([
    # keep-sorted start
    FieldTrial('WebRTC-Aec3BufferingMaxAllowedExcessRenderBlocksOverride',
               337900458,
               date(2024, 9, 1)),
    FieldTrial('WebRTC-Agc2MaxSpeechLevelExperimental',
               457791164,
               date(2026, 12, 11)),
    FieldTrial('WebRTC-Agc2SpeechLevelEstimatorExperimental',
               457791164,
               date(2026, 11, 5)),
    FieldTrial('WebRTC-ApmEnforce48kHzProcessingRate',
               454695115,
               date(2026, 6, 1)),
    FieldTrial('WebRTC-Audio-OpusDecodeStereoByDefault',
               379996136,
               date(2025, 11, 15)),
    FieldTrial('WebRTC-Audio-OpusGeneratePlc',
               42223518,
               date(2025, 4, 1)),
    FieldTrial('WebRTC-Audio-PriorityBitrate',
               42226125,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Av1-GetEncoderInfoOverride',
               42225234,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BitrateAdjusterUseNewfangledHeadroomAdjustment',
               349561566,
               date(2025, 8, 26)),
    FieldTrial('WebRTC-Bwe-LimitPacingFactorByUpperLinkCapacityEstimate',
               42220543,
               date(2025, 1, 1)),
    FieldTrial('WebRTC-Bwe-ResetOnAdapterIdChange',
               42225231,
               date(2025, 5, 30)),
    FieldTrial('WebRTC-Bwe-ScreamV2',
               447037083,
               date(2026, 12, 30)),
    FieldTrial('WebRTC-ClockAligner',
               555794212,
               date(2027, 4, 1)),
    FieldTrial('WebRTC-CorruptionDetectionFrameSelector',
               358039777,
               date(2026, 4, 1)),
    FieldTrial('WebRTC-DataChannelMessageInterleaving',
               41481008,
               date(2024, 10, 1)),
    FieldTrial('WebRTC-Dav1dDecoder-CropToRenderResolution',
               405341160,
               date(2026, 3, 21)),
    FieldTrial('WebRTC-DisableSslGroupIds',
               404763475,
               date(2025,9,1)),
    FieldTrial('WebRTC-ElasticBitrateAllocation',
               350555527,
               date(2025, 3, 1)),
    FieldTrial('WebRTC-EnableDtlsPqc',
               404763475,
               date(2026,6,1)),
    FieldTrial('WebRTC-EncoderDataDumpDirectory',
               296242528,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-EncoderSpeed',
               443906251,
               date(2026, 4, 6)),
    FieldTrial('WebRTC-EnforceTransceiverDirection',
               448408148,
               date(2026, 6, 1)),
    FieldTrial('WebRTC-ErrorOnRtpExtensionRedefinition',
               504685269,
               date(2027, 1, 1)),
    FieldTrial('WebRTC-H265-QualityScaling',
               402154973,
               date(2026, 1, 1)),
    FieldTrial('WebRTC-HeaderExtensionNegotiateMemory',
               439514253,
               date(2026, 2, 18)),
    FieldTrial('WebRTC-IPv6NetworkResolutionFixes',
               42224598,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IceHandshakeDtls',
               367395350,
               date(2026, 1, 1)),
    FieldTrial('WebRTC-IncomingTimestampOnMarkerBitOnly',
               42224805,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IncreaseIceCandidatePriorityHostSrflx',
               42225331,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-JitterEstimatorConfig',
               42224404,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-LibaomAv1Encoder-DropRepeatFramesOnEnhancementLayers',
               445115234,
               date(2026, 3, 19)),
    FieldTrial('WebRTC-LibvpxVp8Encoder-AndroidSpecificThreadingSettings',
               42226191,
               date(2024, 9, 1)),
    FieldTrial('WebRTC-LibvpxVp9Encoder-PostEncodeFrameDrop',
               500517546,
               date(2027, 1, 1)),
    FieldTrial('WebRTC-MediaTaskQueuePriorities',
               470337728,
               date(2027, 4, 1)),
    FieldTrial('WebRTC-MixedCodecSimulcast',
               362277533,
               date(2025, 9, 1)),
    FieldTrial('WebRTC-NoSdpMangleAllowForTesting',
               40567530,
               date(2026, 6, 1)),
    FieldTrial('WebRTC-NoSdpMangleReject',
               40567530,
               date(2026, 6, 1)),
    FieldTrial('WebRTC-NoSdpMangleUfrag',
               375571816,
               date(2025, 10, 11)),
    FieldTrial('WebRTC-NoSdpMangleUfragRestrictedAddresses',
               409713509,
               date(2025, 10, 11)),
    FieldTrial('WebRTC-Pacer-FastRetransmissions',
               40235589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PayloadTypesInTransport',
               360058654,
               date(2025, 9, 11)),
    FieldTrial('WebRTC-PostFilterKillSwitch',
               417439718,
               date(2025, 9, 1)),
    FieldTrial('WebRTC-QCM-Dynamic-AV1',
               349860657,
               date(2025, 7, 1)),
    FieldTrial('WebRTC-QCM-Dynamic-VP8',
               349860657,
               date(2025, 7, 1)),
    FieldTrial('WebRTC-QCM-Dynamic-VP9',
               349860657,
               date(2025, 7, 1)),
    FieldTrial('WebRTC-QCM-Static-AV1',
               349860657,
               date(2025, 7, 1)),
    FieldTrial('WebRTC-QCM-Static-VP8',
               349860657,
               date(2025, 7, 1)),
    FieldTrial('WebRTC-QCM-Static-VP9',
               349860657,
               date(2025, 7, 1)),
    FieldTrial('WebRTC-RFC8888CongestionControlFeedback',
               42225697,
               date(2027, 2, 1)),
    FieldTrial('WebRTC-ReceiveBufferSize',
               42225927,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RetransmitFlushedPackets',
               564720400,
               date(2027, 4, 1)),
    FieldTrial('WebRTC-RtcEventLogEncodeNetEqSetMinimumDelayKillSwitch',
               42225058,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RtcpXrReceiverReferenceTime',
               516205747,
               date(2027, 12, 1)),
    FieldTrial('WebRTC-Sctp-Snap',
               426480601,
               date(2026, 1, 1)),
    FieldTrial('WebRTC-SdpBundlePayloadTypeCollisionCheck',
               42224689,
               date(2027, 1, 1)),
    FieldTrial('WebRTC-SimulcastEncoderAdapter-DropUnalignedResolution',
               415329365,
               date(2025, 11, 2)),
    FieldTrial('WebRTC-SrtpRemoveReceiveStream',
               42225949,
               date(2024, 10, 1)),
    FieldTrial('WebRTC-SwitchEncoderFollowCodecPreferenceOrder',
               378566918,
               date(2025, 5, 1)),
    FieldTrial('WebRTC-SynchronousDestructors',
               443588673,
               date(2026, 7, 1)),
    FieldTrial('WebRTC-TimestampExtrapolatorConfig',
               424739326,
               date(2026, 6, 30)),
    FieldTrial('WebRTC-UnifiedCommunications',
               466507512,
               date(2026, 6, 30)),
    FieldTrial('WebRTC-UpdateNetworkRouteOnSrtpActivation',
               461532446,
               date(2026, 12, 30)),
    FieldTrial('WebRTC-UseAbsCapTimeForG2gMetric',
               401512883,
               date(2025, 9, 10)),
    FieldTrial('WebRTC-UseWildcardInSdp',
               500638469,
               date(2027, 4, 1)),
    FieldTrial('WebRTC-VP8-MaxFrameInterval',
               42225870,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-SvcForSimulcast',
               347737882,
               date(2024, 10, 1)),
    FieldTrial('WebRTC-Video-EnableRetransmitAllLayers',
               42225262,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-EncoderFallbackSettings',
               42231704,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-H26xPacketBuffer',
               41480904,
               date(2024, 6, 1)),
    FieldTrial('WebRTC-Video-PerSsrcKeyframes',
               527515375,
               date(2027, 6, 1)),
    FieldTrial('WebRTC-VideoEncoderSettings',
               40252667,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VoIP-NetEqConfig',
               500324590,
               date(2027, 4, 1)),
    # keep-sorted end
])  # yapf: disable

NO_BUG = -1
INDEFINITE = date(datetime.MAXYEAR, 1, 1)

# These field trials precedes the policy in `g3doc/field-trials.md` and are
# therefore not required to follow it. Do not add any new field trials here.
# If you remove an entry you should also update
# POLICY_EXEMPT_FIELD_TRIALS_DIGEST.
POLICY_EXEMPT_FIELD_TRIALS: FrozenSet[FieldTrial] = frozenset([
    # keep-sorted start
    FieldTrial('WebRTC-AddNetworkCostToVpn',
               42223280,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AddPacingToCongestionWindowPushback',
               42220204,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AdjustOpusBandwidth',
               42233664,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3AecStateSubtractorAnalyzerResetKillSwitch',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3AntiHowlingMinimizationKillSwitch',
               150764764,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ClampInstQualityToOneKillSwitch',
               42220991,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ClampInstQualityToZeroKillSwitch',
               42220991,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ConservativeTailFreqResponse',
               42223361,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3DelayEstimateSmoothingDelayFoundOverride',
               42222934,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3DelayEstimateSmoothingOverride',
               42222934,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3EnforceCaptureDelayEstimationDownmixing',
               42221238,
               INDEFINITE),
    FieldTrial(
        'WebRTC-Aec3EnforceCaptureDelayEstimationLeftRightPrioritization',
        42221238,
        INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceConservativeHfSuppression',
               42222109,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3EnforceLowActiveRenderLimit',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNearendSuppressorHfTuning',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNearendSuppressorTuning',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNormalSuppressorHfTuning',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceMoreTransparentNormalSuppressorTuning',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceRapidlyAdjustingNearendSuppressorTunings',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceRapidlyAdjustingNormalSuppressorTunings',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceRenderDelayEstimationDownmixing',
               42221238,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceSlowlyAdjustingNearendSuppressorTunings',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceSlowlyAdjustingNormalSuppressorTunings',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceStationarityProperties',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceStationarityPropertiesAtInit',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3EnforceVeryLowActiveRenderLimit',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3MinErleDuringOnsetsKillSwitch',
               42220385,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3NonlinearModeReverbKillSwitch',
               42222109,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3OnsetDetectionKillSwitch',
               42221578,
               INDEFINITE),
    FieldTrial(
        'WebRTC-Aec3RenderDelayEstimationLeftRightPrioritizationKillSwitch',
        42221238,
        date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SensitiveDominantNearendActivation',
               42221578,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3ShortHeadroomKillSwitch',
               42220385,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3StereoContentDetectionKillSwitch',
               42225201,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorAntiHowlingGainOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendEnrExitThresholdOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendEnrThresholdOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendHoldDurationOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendSnrThresholdOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorDominantNearendTriggerThresholdOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendHfMaskSuppressOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendHfMaskTransparentOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendLfMaskSuppressOverride',
               42221589,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3SuppressorNearendLfMaskTransparentOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNearendMaxDecFactorLfOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNearendMaxIncFactorOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalHfMaskSuppressOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalHfMaskTransparentOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalLfMaskSuppressOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalLfMaskTransparentOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalMaxDecFactorLfOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorNormalMaxIncFactorOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3SuppressorTuningOverride',
               42221589,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3TransparentAntiHowlingGain',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3TransparentModeHmm',
               42222401,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3Use1Dot2SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3Use1Dot6SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3Use2Dot0SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot1SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot2SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot3SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot6SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseDot9SecondsInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseErleOnsetCompensationInDominantNearend',
               42222842,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseLowEarlyReflectionsDefaultGain',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseLowLateReflectionsDefaultGain',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseNearendReverbLen',
               42223329,
               INDEFINITE),
    FieldTrial('WebRTC-Aec3UseShortConfigChangeDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3UseZeroInitialStateDuration',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Aec3VerySensitiveDominantNearendActivation',
               42221578,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Agc2SimdAvx2KillSwitch',
               42232605,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Agc2SimdNeonKillSwitch',
               42232605,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Agc2SimdSse2KillSwitch',
               42232605,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AllowMACBasedIPv6',
               41480878,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AlrDetectorParameters',
               42220590,
               INDEFINITE),
    FieldTrial('WebRTC-AndroidNetworkMonitor-IsAdapterAvailable',
               42223964,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ApmExperimentalMultiChannelCaptureKillSwitch',
               42225202,
               INDEFINITE),
    FieldTrial('WebRTC-ApmExperimentalMultiChannelRenderKillSwitch',
               42225203,
               INDEFINITE),
    FieldTrial('WebRTC-Audio-2ndAgcMinMicLevelExperiment',
               40207112,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-ABWENoTWCC',
               42233370,
               INDEFINITE),
    FieldTrial('WebRTC-Audio-AdaptivePtime',
               40694579,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-Allocation',
               42220324,
               INDEFINITE),
    FieldTrial('WebRTC-Audio-AlrProbing',
               42220234,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-LegacyOverhead',
               42221084,
               INDEFINITE),
    FieldTrial('WebRTC-Audio-MinimizeResamplingOnMobile',
               42231221,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-NetEqDelayManagerConfig',
               42220376,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-NetEqSmartFlushing',
               42222334,
               date(2027, 4, 1)),
    FieldTrial('WebRTC-Audio-OpusBitrateMultipliers',
               42221139,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-Red-For-Opus',
               42221750,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Audio-iOS-Holding',
               42233253,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-AudioDevicePlayoutBufferSizeFactor',
               42221006,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BindUsingInterfaceName',
               42220770,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-AllocationProbing',
               42220440,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-EstimateBoundedIncrease',
               42220543,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-IgnoreProbesLowerThanNetworkStateEstimate',
               42220543,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-InjectedCongestionController',
               'webrtc:8415',
               INDEFINITE),
    FieldTrial('WebRTC-Bwe-LimitProbesLowerThanThroughputEstimate',
               42221601,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-LossBasedBweV2',
               42222865,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-MaxRttLimit',
               42234928,
               INDEFINITE),
    FieldTrial('WebRTC-Bwe-MinAllocAsLowerBound',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ProbingBehavior',
               42220440,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ProbingConfiguration',
               42220440,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-ReceiverLimitCapsOnly',
               42222445,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-RobustThroughputEstimatorSettings',
               42220312,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-SafeResetOnRouteChange',
               42234928,
               INDEFINITE),
    FieldTrial('WebRTC-Bwe-SeparateAudioPackets',
               42221011,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Bwe-TrendlineEstimatorSettings',
               42221011,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweBackOffFactor',
               42233342,
               INDEFINITE),
    FieldTrial('WebRTC-BweLossExperiment',
               42230863,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweRapidRecoveryExperiment',
               42233136,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweThroughputWindowConfig',
               42220312,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-BweWindowSizeInPackets',
               42233342,
               INDEFINITE),
    FieldTrial('WebRTC-CongestionWindow',
               42225197,
               INDEFINITE),
    FieldTrial('WebRTC-CpuLoadEstimator',
               42233645,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Debugging-RtpDump',
               42220735,
               INDEFINITE),
    FieldTrial('WebRTC-DecoderDataDumpDirectory',
               42224491,
               INDEFINITE),
    FieldTrial('WebRTC-DependencyDescriptorAdvertised',
               42220386,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DisableUlpFecExperiment',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DontIncreaseDelayBasedBweInAlr',
               42220590,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-DscpFieldTrial',
               42223835,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ExtraICEPing',
               42220311,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FilterAbsSendTimeExtension',
               42220271,
               INDEFINITE),
    FieldTrial('WebRTC-FindNetworkHandleWithoutIpv6TemporaryPart',
               42221149,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FlexFEC-03',
               42230680,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-FlexFEC-03-Advertised',
               42230680,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ForcePlayoutDelay',
               42222016,
               INDEFINITE),
    FieldTrial('WebRTC-ForceSendPlayoutDelay',
               42222016,
               INDEFINITE),
    FieldTrial('WebRTC-ForceSimulatedOveruseIntervalMs',
               42224494,
               INDEFINITE),
    FieldTrial('WebRTC-FrameDropper',
               42234921,
               INDEFINITE),
    FieldTrial('WebRTC-FullBandHpfKillSwitch',
               42221279,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-GenericCodecDependencyDescriptor',
               42225273,
               INDEFINITE),
    FieldTrial('WebRTC-GenericDescriptorAdvertised',
               42234553,
               INDEFINITE),
    FieldTrial('WebRTC-GenericDescriptorAuth',
               42220132,
               INDEFINITE),
    FieldTrial('WebRTC-GenericPictureId',
               42234553,
               INDEFINITE),
    FieldTrial('WebRTC-GetEncoderInfoOverride',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IPv6Default',
               NO_BUG,  # chromium:413437
               date(2024, 4, 1)),
    FieldTrial('WebRTC-IceControllerFieldTrials',
               40658968,
               INDEFINITE),
    FieldTrial('WebRTC-IceFieldTrials',
               42221103,
               INDEFINITE),
    FieldTrial('WebRTC-KeyframeInterval',
               42220470,
               INDEFINITE),
    FieldTrial('WebRTC-LegacySimulcastLayerLimit',
               42233936,
               INDEFINITE),
    FieldTrial('WebRTC-LowresSimulcastBitrateInterpolation',
               42222558,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-MutedStateKillSwitch',
               177830919,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Network-UseNWPathMonitor',
               42221045,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-NetworkMonitorAutoDetect',
               42223964,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-NormalizeSimulcastResolution',
               NO_BUG,
               INDEFINITE),
    FieldTrial('WebRTC-Pacer-BlockAudio',
               42233548,
               INDEFINITE),
    FieldTrial('WebRTC-Pacer-DrainQueue',
               42233548,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Pacer-IgnoreTransportOverhead',
               42235102,
               INDEFINITE),
    FieldTrial('WebRTC-Pacer-PadInSilence',
               42233548,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PacketBufferMaxSize',
               42235070,
               INDEFINITE),
    FieldTrial('WebRTC-PcFactoryDefaultBitrates',
               42220941,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PiggybackIceCheckAcknowledgement',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-PixelLimitResource',
               42222397,
               INDEFINITE),
    FieldTrial('WebRTC-ProbingScreenshareBwe',
               42232804,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-ProtectionOverheadRateThreshold',
               42225198,
               INDEFINITE),
    FieldTrial('WebRTC-QpParsingKillSwitch',
               42222690,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-RtcEventLogKillSwitch',
               42222210,
               INDEFINITE),
    FieldTrial('WebRTC-SendBufferSizeBytes',
               42222026,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SetSocketReceiveBuffer',
               42223976,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SimulcastEncoderAdapter-GetEncoderInfoOverride',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SimulcastLayerLimitRoundUp',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SpsPpsIdrIsH264Keyframe',
               42233557,
               INDEFINITE),
    FieldTrial('WebRTC-StrictPacingAndProbing',
               42233198,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SurfaceCellularTypes',
               42221576,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-SwitchEncoderOnInitializationFailures',
               42223783,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseBaseHeavyVP8TL3RateAllocation',
               42234670,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseDifferentiatedCellularCosts',
               42221576,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseStandardBytesStats',
               42220573,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-UseTurnServerAsStunServer',
               42221142,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8-ForcePartitionResilience',
               42221952,
               INDEFINITE),
    FieldTrial('WebRTC-VP8-Forced-Fallback-Encoder-v2',
               42231704,
               INDEFINITE),
    FieldTrial('WebRTC-VP8-GetEncoderInfoOverride',
               42221952,
               INDEFINITE),
    FieldTrial('WebRTC-VP8-Postproc-Config',
               42221657,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP8-Postproc-Config-Arm',
               42231704,
               INDEFINITE),
    FieldTrial('WebRTC-VP8IosMaxNumberOfThread',
               42220027,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-GetEncoderInfoOverride',
               NO_BUG,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-LowTierOptimizations',
               42224122,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9-PerformanceFlags',
               42221657,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VP9QualityScaler',
               42221411,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-BalancedDegradation',
               42232717,
               INDEFINITE),
    FieldTrial('WebRTC-Video-BalancedDegradationSettings',
               NO_BUG,
               INDEFINITE),
    FieldTrial('WebRTC-Video-DisableAutomaticResize',
               42221931,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-DiscardPacketsWithUnknownSsrc',
               42235091,
               INDEFINITE),
    FieldTrial('WebRTC-Video-ForcedSwDecoderFallback',
               NO_BUG,
               INDEFINITE),
    FieldTrial('WebRTC-Video-InitialDecoderResolution',
               42222018,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-MinVideoBitrate',
               42220993,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-Pacing',
               42220062,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-Video-PreferTemporalSupportOnBaseLayer',
               42221417,
               INDEFINITE),
    FieldTrial('WebRTC-Video-QualityScalerSettings',
               NO_BUG,
               INDEFINITE),
    FieldTrial('WebRTC-Video-QualityScaling',
               42234348,
               INDEFINITE),
    FieldTrial('WebRTC-Video-UseFrameRateForOverhead',
               166341943,
               date(2024, 4, 1)),
    FieldTrial('WebRTC-VideoFrameTrackingIdAdvertised',
               42222747,
               INDEFINITE),
    FieldTrial('WebRTC-VideoLayersAllocationAdvertised',
               42222126,
               INDEFINITE),
    FieldTrial('WebRTC-VideoRateControl',
               42220259,
               INDEFINITE),
    FieldTrial('WebRTC-Vp9InterLayerPred',
               NO_BUG,  # chromium:949536
               INDEFINITE),
    FieldTrial('WebRTC-Vp9IssueKeyFrameOnLayerDeactivation',
               40595338,
               date(2024, 4, 1)),
    # keep-sorted end
])  # yapf: disable

POLICY_EXEMPT_FIELD_TRIALS_DIGEST: str = \
    'feaa209731fde7f52505536924935940cc57dccd'

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
            'you should also update POLICY_EXEMPT_FIELD_TRIALS_DIGEST. The '
            f'new digest is "{sha1.hexdigest()}".')

    for trial in field_trials:
        if not trial.key.startswith('WebRTC-'):
            invalid.append(f'{trial.key} does not start with "WebRTC-".')
        if trial.bug_id <= 0:
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
