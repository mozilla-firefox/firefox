export const FeatureManifest: {
    "no-feature-firefox-desktop": {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        allowCoenrollment: boolean;
        variables: {};
    };
    testFeature: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        isEarlyStartup: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
            testInt: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            testSetString: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    testPlatformExposureFeature: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        exposureDescription: string;
        isEarlyStartup: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
        };
    };
    "nimbus-qa-1": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            value: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "nimbus-qa-2": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            value: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    prefFlips: {
        description: string;
        owner: string;
        hasExposure: boolean;
        allowCoenrollment: boolean;
        variables: {
            prefs: {
                type: string;
                description: string;
            };
        };
        schema: {
            uri: string;
            path: string;
        };
    };
    search: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            scotchBonnetEnableOverride: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            trendingRequireSearchMode: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            trendingMaxResultsNoSearchMode: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            targetExperiment: {
                type: string;
                description: string;
            };
            visualSearchEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            highlightToSearchEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            highlightToSearchIgnoredSites: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            highlightToSearchDetectPageMenus: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    searchConfiguration: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            experiment: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            extraParams: {
                type: string;
                description: string;
            };
            separatePrivateDefaultUIEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            separatePrivateDefaultUrlbarResultEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    urlbar: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            actionsOnboardingTimesToShow: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            addonsFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            addonsShowLessFrequentlyCap: {
                type: string;
                description: string;
            };
            allowSearchSuggestionsForSimpleOrigins: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            ampFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            ampMatchingStrategy: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            autoFillAdaptiveHistoryEnabled: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            autoFillAdaptiveHistoryMinCharsThreshold: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            autoFillAdaptiveHistoryUseCountThreshold: {
                type: string;
                description: string;
            };
            autoFillAdaptiveHistoryUrlMinPicks: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            autoFillAdaptiveHistoryUrlPicksAgeDays: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            autoFillBackspaceBlockDurationMs: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            autoFillBackspaceThreshold: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            autoFillDismissalBlockDurationMs: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            contextMenuFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            deduplicationEnabled: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            fakespotFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            flightStatusFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            importantDatesFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            keywordExposureResults: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            marketFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            mdnFeatureGate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            merinoClientVariants: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            merinoEndpointURL: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            merinoOhttpConfigURL: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            merinoOhttpRelayURL: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            merinoProviders: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            merinoTimeoutMs: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            newtabFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            newtabVariantA: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            newtabVariantB: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            exposureResults: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            showExposureResults: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            quickSuggestAmpTopPickCharThreshold: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestAmpTopPickScoreForLargeIcon: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            quickSuggestDynamicSuggestionTypes: {
                type: string;
                setPref: {
                    pref: string;
                    branch: string;
                };
                description: string;
            };
            quickSuggestEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestImpressionCapsSponsoredEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestImpressionCapsNonSponsoredEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestMlInitDelaySeconds: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestMlEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestNonSponsoredEnabled: {
                type: string;
                description: string;
            };
            quickSuggestNonSponsoredIndex: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestOnlineAvailable: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestRankingMode: {
                type: string;
                fallbackPref: string;
                description: string;
                enum: string[];
            };
            quickSuggestScoreMap: {
                type: string;
                description: string;
            };
            quickSuggestSettingsUi: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestSponsoredEnabled: {
                type: string;
                description: string;
            };
            quickSuggestSponsoredIndex: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            quickSuggestSponsoredPriority: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            realtimeMinKeywordLength: {
                type: string;
                description: string;
            };
            realtimeShowLessFrequentlyCap: {
                type: string;
                description: string;
            };
            recentSearchesFeatureGate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            recentSearchesMaxResults: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            resultExplanationsFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            resultTargetMenuFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            semanticHistoryEnable: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryDefaultBatchChunksize: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryMaxChunkTimeMs: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryMaxChunkTimeMsFailsafe: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryMinEntriesBeforeThrottle: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryDistanceThreshold: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistorySmartwindowDistanceThreshold: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistorySupportedRegions: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryEmbeddingType: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryMultilingualEmbeddingRegions: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryDomainScopedSearch: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            semanticHistoryInSeparateGroup: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            showDebuggingIcons: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            sportsFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            suggestSemanticHistoryMinLength: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            suggestCalculator: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            temporalFeatureGate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            trackerCountFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            unitConversionEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            weatherFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            weatherKeywordsMinimumLength: {
                type: string;
                description: string;
            };
            weatherShowLessFrequentlyCap: {
                type: string;
                description: string;
            };
            weatherUiTreatment: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            wikipediaFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            yelpMinKeywordLength: {
                type: string;
                description: string;
            };
            yelpMlEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            yelpFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            yelpServiceResultDistinction: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            yelpShowLessFrequentlyCap: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            yelpSuggestNonPriorityIndex: {
                type: string;
                description: string;
            };
            yelpSuggestPriority: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            yelpRealtimeFeatureGate: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            originsAlternativeEnable: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            originsDaysCutOff: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesAlternativeEnable: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesNumSampledVisits: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesHalfLifeDays: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesMaxVisitGap: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesViewTimeSeconds: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pageViewTimeIfManyKeypressesSeconds: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            manyKeypresses: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesVeryHighWeight: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesHighWeight: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesMediumWeight: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pagesLowWeight: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    aboutwelcome: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        isEarlyStartup: boolean;
        variables: {
            enabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            id: {
                type: string;
                description: string;
            };
            screens: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            languageMismatchEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            transitions: {
                type: string;
                description: string;
            };
            backdrop: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            toolbarButtonEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    preonboarding: {
        description: string;
        owner: string;
        hasExposure: boolean;
        isEarlyStartup: boolean;
        variables: {
            enabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            screens: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            requireAction: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            currentVersion: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            minimumVersion: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            firstRunURL: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    moreFromMozilla: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            template: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    windowsLaunchOnLogin: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    pushNotificationHelper: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        variables: {
            available: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    firefoxBridge: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    abouthomecache: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    diskCache: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            diskCacheEnabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            diskCacheEncryptionEnabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    newtab: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        isEarlyStartup: boolean;
        variables: {
            newTheme: {
                type: string;
                description: string;
            };
            customizationMenuEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            prefsButtonIcon: {
                type: string;
                description: string;
            };
            topSitesContileEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            topSitesUseAdditionalTilesFromContile: {
                type: string;
                description: string;
            };
        };
    };
    newtabAdSizingExperiment: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            leaderboard: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            leaderboard_position: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            billboard: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            billboard_position: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            medium_rectangle: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabPromoCard: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    newtabAdsReporting: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            reportAdsEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            reportAdsNewReasonsEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabContextualContent: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            contextualContentEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            localeContextualContentConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            regionContextualContentConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            contextualContentFeeds: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            contextualContentSelectedFeed: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabCustomWallpaper: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            uploadWallpaper: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            colorPicker: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            maxFileSizeEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            maxFileSize: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabInlineTopicSelection: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            TopicSelectionEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabMarsOhttp: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabMerinoOhttp: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabOhttpImages: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
            includeTopStoriesSection: {
                type: string;
                description: string;
            };
        };
    };
    newtabMobileDownloadPromotion: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            showModal: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            variantA: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            variantB: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            variantC: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabPrivatePing: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            privatePingEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            includeInferredInterests: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            maxSubmissionDelayMs: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            minSubmissionDelayMs: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            dwellTimeEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            dwellTimeNoiseScale: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabPublisherFavicons: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newTabSectionsExperiment: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            sectionsEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            cardRefreshEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            regionPersonalizationInferredConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            localePersonalizationInferredConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            personalizationInferredEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            localeSectionstConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            regionSectionsConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            sectionsPersonalization: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            sectionsCustomizeMenuPanel: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            sectionsContextualAdsEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            regionSectionsContextualAdsConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            localeSectionsContextualAdsConfig: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            contextualSpocPlacements: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            contextualSpocCounts: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            contextualBannerPlacements: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            contextualBannerCounts: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabInferredPersonalization: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            iv_refresh_frequency_hours: {
                type: string;
                description: string;
            };
            history_cull_days: {
                type: string;
                description: string;
            };
            normalized_time_zone_offset: {
                type: string;
                description: string;
            };
            local_popular_today_rerank: {
                type: string;
                description: string;
            };
            local_inferred_weight: {
                type: string;
                description: string;
            };
            server_inferred_weight: {
                type: string;
                description: string;
            };
        };
    };
    newtabSmartShortcuts: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
            force_log: {
                type: string;
                description: string;
            };
            over_sample_multiplier: {
                type: string;
                description: string;
            };
            positive_prior: {
                type: string;
                description: string;
            };
            negative_prior: {
                type: string;
                description: string;
            };
            thom_weight: {
                type: string;
                description: string;
            };
            frec_weight: {
                type: string;
                description: string;
            };
            hour_weight: {
                type: string;
                description: string;
            };
            daily_weight: {
                type: string;
                description: string;
            };
            bmark_weight: {
                type: string;
                description: string;
            };
            rece_weight: {
                type: string;
                description: string;
            };
            freq_weight: {
                type: string;
                description: string;
            };
            refre_weight: {
                type: string;
                description: string;
            };
            bias_weight: {
                type: string;
                description: string;
            };
            fset: {
                type: string;
                description: string;
            };
            eta: {
                type: string;
                description: string;
            };
            click_bonus: {
                type: string;
                description: string;
            };
        };
    };
    newtabSpocsCache: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            spocsCacheTimeout: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            spocsStartupCache: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    newtabSponsoredContent: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            spocPositions: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            spocPlacements: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            spocCounts: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            tilesPlacements: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            tilesCounts: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    adsBackend: {
        description: string;
        owner: string;
        hasExposure: boolean;
        allowCoenrollment: boolean;
        variables: {
            flags: {
                type: string;
                description: string;
            };
        };
    };
    newtabTrainhop: {
        description: string;
        owner: string;
        hasExposure: boolean;
        allowCoenrollment: boolean;
        variables: {
            type: {
                type: string;
                description: string;
            };
            payload: {
                type: string;
                description: string;
            };
        };
    };
    newtabTrainhopAddonDeployment: {
        description: string;
        owner: string;
        hasExposure: boolean;
        allowCoenrollment: boolean;
        exposureDescription: string;
        variables: {
            xpi_download_path: {
                type: string;
                description: string;
            };
            addon_version: {
                type: string;
                description: string;
            };
        };
    };
    newtabTrainhopFirstStartup: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
        };
    };
    newtabTopicSelection: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            availableTopics: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            suggestedTopics: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            topicSelectionOnboarding: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            regionTopicsConfig: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            localeTopicsConfig: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            regionTopicLabelConfig: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            localeTopicLabelConfig: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    newtabUnifiedAds: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            unifiedAdsEndpoint: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            unifiedAdsSpocsEnabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            unifiedAdsTilesEnabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            unifiedAdsFeedEnabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            unifiedAdsFeedTilesEnabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    newtabWidgets: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
            listsEnabled: {
                type: string;
                description: string;
            };
            timerEnabled: {
                type: string;
                description: string;
            };
            clocksEnabled: {
                type: string;
                description: string;
            };
            listsBadgeEnabled: {
                type: string;
                description: string;
            };
            listsBadgeLabel: {
                type: string;
                description: string;
            };
        };
    };
    pocketNewtab: {
        description: string;
        owner: string;
        hasExposure: boolean;
        isEarlyStartup: boolean;
        variables: {
            contileTopsitesPositions: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            spocAdTypes: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            spocZoneIds: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            spocSiteId: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            widgetPositions: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            hybridLayout: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            hideCardBackground: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            fourCardLayout: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            newFooterSection: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            hideDescriptions: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            hideDescriptionsRegions: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            compactGrid: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            compactImages: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            imageGradient: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            titleLines: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            descLines: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            readTime: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            newSponsoredLabel: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            wallpapers: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            wallpapersHighlightEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            wallpaperHighlightHeaderText: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            wallpaperHighlightContentText: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            wallpaperHighlightCtaText: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            currentWallpaper: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            weatherLocationSearch: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            spocsEndpoint: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            spocsEndpointAllowlist: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ctaButtonSponsors: {
                description: string;
                type: string;
                fallbackPref: string;
            };
            ctaButtonVariant: {
                description: string;
                type: string;
                fallbackPref: string;
            };
            regionStoriesConfig: {
                description: string;
                type: string;
                fallbackPref: string;
            };
            merinoProviderEndpoint: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            regionStoriesBlock: {
                description: string;
                type: string;
                fallbackPref: string;
            };
            regionSpocsConfig: {
                description: string;
                type: string;
                fallbackPref: string;
            };
            regionWeatherConfig: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            localeWeatherConfig: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            topSitesMaxSponsored: {
                type: string;
                description: string;
            };
            topSitesContileMaxSponsored: {
                type: string;
                description: string;
            };
            pocketFeedParameters: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            merinoFeedExperiment: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    sessionRestore: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            startupBehavior: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    sessionRestoreNewTab: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            showSetting: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    screenshots: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            buttonOnToolbarByDefault: {
                type: string;
                description: string;
            };
        };
    };
    "password-autocomplete": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            directMigrateSingleProfile: {
                type: string;
                description: string;
            };
        };
    };
    "address-autofill-feature": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            status: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "creditcards-autofill-enabled": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            creditcardsSupported: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "creditcards-cvv-capture": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            cvvSupported: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "autofill-ml": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            twoHeadEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            engineTimeoutMS: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            features: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            modelVersion: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "autofill-ml-ignore-field-types": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            ignoreFieldTypes: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "autofill-suppress-unrecognized-autocomplete": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "autocomplete-remove-records": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "autofill-addresses-rust-storage": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "autofill-credit-cards-rust-storage": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    shellService: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            disablePin: {
                type: string;
                description: string;
            };
            disableStartMenuPin: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            setDefaultBrowserUserChoice: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            setDefaultBrowserUserChoiceRegRename: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            setDefaultPDFHandler: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            setDefaultPDFHandlerOnlyReplaceBrowsers: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            setDefaultGuidanceNotifications: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            focusSetDefaultBrowserButton: {
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                type: string;
            };
            displayKitImageBehindSetDefaultBrowserButton: {
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                type: string;
                enum: string[];
            };
        };
    };
    upgradeDialog: {
        description: string;
        owner: string;
        hasExposure: boolean;
        isEarlyStartup: boolean;
        variables: {
            enabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    cfr: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "moments-page": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    includeForWebPush: {
        description: string;
        owner: string;
        hasExposure: boolean;
        allowCoenrollment: boolean;
        variables: {};
    };
    infobar: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    spotlight: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    featureCallout: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    privacySecurityMessaging: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            blockingMilestoneMessagingEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            blockingMilestones: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            blockingMilestoneUpdateInterval: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    fullPageTranslation: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            boolean: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    fullPageTranslationAutomaticPopup: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            boolean: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    selectTranslation: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    translationsQuickAction: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    pdfjs: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            enableAltText: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enableUpdatedAddImage: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            browserMlEnable: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enableSignatureEditor: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enableSignatureVerification: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "fxms-message": {
        description: string;
        owner: string;
        allowCoenrollment: boolean;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-1": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-2": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-3": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-4": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-5": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-6": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-7": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-8": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-9": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-10": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-11": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-12": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-13": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-14": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-15": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-16": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-17": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-18": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-19": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-20": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-21": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-22": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-23": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-24": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    "fxms-message-25": {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    whatsNewPage: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            overrideUrl: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            maxVersion: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            minVersion: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            disableWNP: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    pbNewtab: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    privateWindowRedesign: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            pbwRedesignGate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    protectionsPanelRedesign: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            protectionsPanelRedesignGate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    backgroundTaskMessage: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    backgroundUpdateAutomaticRestart: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    backgroundUpdateCheckPolicy: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        variables: {
            throttleEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            throttleAfterDays: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            throttleDebouncePeriodInHours: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    pictureinpicture: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            title: {
                type: string;
                description: string;
            };
            message: {
                type: string;
                description: string;
            };
            showIconOnly: {
                type: string;
                description: string;
            };
            oldToggle: {
                type: string;
                description: string;
            };
            displayDuration: {
                type: string;
                description: string;
            };
        };
    };
    glean: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        allowCoenrollment: boolean;
        variables: {
            gleanMetricConfiguration: {
                type: string;
                description: string;
            };
        };
    };
    gleanInternalSdk: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        variables: {
            finalInactive: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gleanMetricConfiguration: {
                type: string;
                description: string;
            };
            gleanMaxPingsPerMinute: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enableFOGTransport: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    legacyTelemetry: {
        description: string;
        owner: string;
        applications: string[];
        hasExposure: boolean;
        variables: {
            disabledPings: {
                type: string;
                description: string;
            };
            disableMainPingScalars: {
                type: string;
                description: string;
            };
            disableMainPingHgrams: {
                type: string;
                description: string;
            };
        };
    };
    browserLowMemoryPrefs: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            lowMemoryResponseMask: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            lowMemoryResponseOnWarn: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            tabsUnloadOnLowMemory: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    echPrefs: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            tlsEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            h3Enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            forceWaitHttpsRR: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            insecureFallback: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            tlsGreaseProb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            h3GreaseEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            disableGreaseOnFallback: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            greasePaddingSize: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    dohPrefs: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            trrMode: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            trrUri: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dohMode: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dohUri: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dohProviderList: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dohProviderSteeringList: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            nativeHTTPSRecords: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    dooh: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            ohttpEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ohttpRelayUri: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ohttpConfigUri: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ohttpUri: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networking: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            preconnect: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enableOffMainThreadStreamDecompression: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            offMainThreadStreamDecompressionThreshold: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            http3UseNSPRForIO: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            prefetchNext: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingEarlyHints: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            ehPreloadEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ehPreconnectEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingDNS: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            dnsMaxPriorityThreads: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dnsMaxAnyPriorityThreads: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            maxDnsCacheEntries: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dnsGracePeriod: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dnsNegativeCacheExpiration: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingCongestionControl: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            http3CCalgorithm: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            http3SlowStartAlgorithm: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            http3HyStartAlternativeCssBaseline: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            http3SpuriousRecovery: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            http3Abe: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            http3EcnMark: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingConnections: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            httpMaxConnections: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpMaxPersistentConnectionsPerServer: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpMaxUrgentStartExcessiveConnectionsPerHost: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            speculativeConnectionLimit: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            happyEyeballsEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            happyEyeballsResolutionDelay: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            happyEyeballsConnectionAttemptDelay: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingSendOnDataFinished: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            sendOnDataFinished: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sendOnDataFinishedFromInputStreamPump: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sendOnDataFinishedToHtml5parser: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sendOnDataFinishedToCssLoader: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingBannedPorts: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            bannedPortList: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    localNetworkAccess: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enableLNAWithETPStrict: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            blockTrackers: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            lnaPromptsEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            lnaFeatureEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            blockInsecureContexts: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    essentialFallbackDomains: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            essentialDomainsEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    disableHttp3: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            withThirdPartyRoots: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkPrioritization: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            priorityHeader: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            fetchPriority: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            h3FetchPriority: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sendNoRFC7540Setting: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            h2deps: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            h3BackgroundTabDeprioritization: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            documentPriorityIncremental: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            imagePriorityIncremental: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            imageAdjustLayoutPriority: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadScriptLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadScriptHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadScriptAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustModuleScriptLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustModuleScriptHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustModuleScriptAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustAsyncOrDeferScriptLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustAsyncOrDeferScriptHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustAsyncOrDeferScriptAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustScriptInHeadLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustScriptInHeadHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustScriptInHeadAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustOtherScriptLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustOtherScriptHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustOtherScriptAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadFontLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadFontHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadFontAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadFetchLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadFetchHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadFetchAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustDeferredStyleLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustDeferredStyleHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustDeferredStyleAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadStyleLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadStyleHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustLinkPreloadStyleAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustNonDeferredStyleLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustNonDeferredStyleHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustNonDeferredStyleAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustGlobalFetchApiLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustGlobalFetchApiHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustGlobalFetchApiAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustImagesLow: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustImagesHigh: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            adjustImagesAuto: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            lowerTrackersPriority: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enableHttpTailing: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpTailingUrgency: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpTailingDelayQuantum: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpTailingDelayQuantumAfterDCL: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpTailingMaxDelay: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            httpTailingTotalMaxDelay: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingAuth: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            redirectForAuthRetriesEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    networkingDenyIpAddrAny: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            denyIpAddrAny: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    compressionDictionaries: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enableDictionaries: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    streamingUpload: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enableFetchStreamingUpload: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    pingsender: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            backgroundTaskEnabled: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    dapTelemetry: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
            task1Enabled: {
                type: string;
                description: string;
            };
            task1TaskId: {
                type: string;
                description: string;
            };
            visitCountingEnabled: {
                type: string;
                description: string;
            };
            visitCountingExperimentList: {
                type: string;
                description: string;
            };
        };
    };
    dapIncrementality: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            measurementType: {
                type: string;
                description: string;
            };
            advertiser: {
                type: string;
                description: string;
            };
            taskId: {
                type: string;
                description: string;
            };
            length: {
                type: string;
                description: string;
            };
            timePrecision: {
                type: string;
                description: string;
            };
            visitCountUrls: {
                type: string;
                description: string;
            };
            referrerUrls: {
                type: string;
                description: string;
            };
            targetUrls: {
                type: string;
                description: string;
            };
            unknownReferrerBucket: {
                type: string;
                description: string;
            };
            unknownReferrerMetricName: {
                type: string;
                description: string;
            };
        };
    };
    dapAggregators: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            leader_url: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            leader_hpke: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            helper_url: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            helper_hpke: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    etpLevel2PBMPref: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    etpStrictFeatures: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            features: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    thirdPartyCookieBlocking: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enabledPBM: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    googleSafeBrowsingV5: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    safeBrowsingRealTime: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            globalCacheEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            simulationEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            simulationCacheTTLSec: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            simulationHitProbability: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            simulationNegativeCacheEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            simulationNegativeCacheTTLSec: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    fxaButtonVisibility: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            boolean: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            pxiToolbarEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            monitorEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            relayEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            vpnEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            avatarIconVariant: {
                description: string;
                type: string;
            };
        };
    };
    fxaClientAssociation: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            pingEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    fxaAppMenuItem: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            ctaCopyVariant: {
                description: string;
                type: string;
            };
        };
    };
    fxaAvatarMenuItem: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            ctaCopyVariant: {
                description: string;
                type: string;
            };
        };
    };
    legacyHeartbeat: {
        description: string;
        owner: string;
        hasExposure: boolean;
        schema: {
            uri: string;
            path: string;
        };
        variables: {
            survey: {
                type: string;
                description: string;
            };
        };
    };
    fingerprintingProtection: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabledNormal: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            enabledPrivate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            overrides: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            fdlibm_math: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            canvas_random_use_siphash: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            enabledBaseline: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            overridesBaseline: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    userCharacteristics: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            currentVersion: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    mediaCapabilities: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            webrtcEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            webrtcAllowlist: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            legacyEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            legacyAllowlist: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    migrationWizard: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            showImportAll: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            showPreferencesEntrypoint: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            aboutWelcomeBehavior: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            migrateExtensions: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            chromeCanRequestPermissions: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    mixedContentUpgrading: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    gc: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            max_nursery_size: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            min_nursery_size: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_allocation_threshold_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_balanced_heap_limits: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_compacting: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_generational: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_heap_growth_factor: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_helper_thread_ratio: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_high_frequency_large_heap_growth: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_high_frequency_small_heap_growth: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_high_frequency_time_limit_ms: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_incremental: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            incremental_weakmap: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_incremental_slice_ms: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_large_heap_incremental_limit: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_large_heap_size_min_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_low_frequency_heap_growth: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_malloc_threshold_base_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_max_empty_chunk_count: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_max_helper_threads: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_min_empty_chunk_count: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_parallel_marking: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_parallel_marking_threshold_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_max_parallel_marking_threads: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_per_zone: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_small_heap_incremental_limit: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_small_heap_size_max_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            gc_urgent_threshold_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            nursery_eager_collection_threshold_kb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            nursery_eager_collection_threshold_percent: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            nursery_eager_collection_timeout_ms: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            nursery_max_time_goal_ms: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    jsParallelParsing: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    jitThresholds: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            blinterp_threshold: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            baseline_threshold: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ion_threshold: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ion_bailout_threshold: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ion_offthread_compilation: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            inlining_max_length: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    jitHintsCache: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    scriptloader: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            bytecode_cache_strategy: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            delazification_strategy: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            delazification_max_size: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            delazification_min_mem: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            speculate_async_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            speculate_link_preload_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            speculate_non_parser_inserted_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            speculate_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            navigation_cache_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    speculationRulesPrefetch: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            dryRun: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    opaqueResponseBlocking: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        isEarlyStartup: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            javascriptValidator: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            filterFetchResponse: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            mediaExceptionsStrategy: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    updatePrompt: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            showReleaseNotesLink: {
                type: string;
                description: string;
            };
            releaseNotesURL: {
                type: string;
                fallbackPref: string;
                description: string;
            };
        };
    };
    powerSaver: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            reduceFrameRates: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            mediaAutoPlay: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            backgroundTimerMinTime: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            backgroundTimerRegenerationRate: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    backgroundUpdate: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            enableUpdatesForUnelevatedInstallations: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    bookmarks: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enableBookmarksToolbar: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            showOtherBookmarks: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    backgroundThreads: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            use_low_power: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            lower_mainthread_priority_in_background: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    phc: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            phcEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcMinRamMB: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcAvgDelayFirst: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcAvgDelayNormal: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcAvgDelayPageReuse: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcAvgDelayContentFirst: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcAvgDelayContentNormal: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcAvgDelayContentPageReuse: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            phcMaxSize: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    mailto: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            dualPrompt: {
                type: string;
                description: string;
                fallbackPref: string;
            };
            "dualPrompt.onLocationChange": {
                type: string;
                description: string;
                fallbackPref: string;
            };
            "dualPrompt.dismissXClickMinutes": {
                type: string;
                description: string;
                fallbackPref: string;
            };
            "dualPrompt.dismissNotNowMinutes": {
                type: string;
                description: string;
                fallbackPref: string;
            };
            dialog: {
                type: string;
                description: string;
            };
        };
    };
    nimbusIsReady: {
        description: string;
        owner: string;
        hasExposure: boolean;
        applications: string[];
        variables: {
            eventCount: {
                description: string;
                type: string;
            };
        };
    };
    nimbusTelemetry: {
        description: string;
        owner: string;
        hasExposure: boolean;
        applications: string[];
        variables: {
            gleanMetricConfiguration: {
                description: string;
                type: string;
            };
            nimbusTargetingEnvironment: {
                description: string;
                type: string;
            };
            reducedEnrollmentStatusTelemetryEnabled: {
                description: string;
                type: string;
            };
        };
        schema: {
            uri: string;
            path: string;
        };
    };
    httpsFirst: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enabledPbm: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            enabledSchemeless: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            backgroundTimerMs: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    netErrorSearchCTA: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    contentRelevancy: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                fallbackPref: string;
            };
            maxInputUrls: {
                description: string;
                type: string;
            };
            minInputUrls: {
                description: string;
                type: string;
            };
            timerInterval: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            ingestEnabled: {
                description: string;
                type: string;
                fallbackPref: string;
            };
        };
    };
    backupService: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            sqlitePagesPerStep: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sqliteStepDelayMs: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            idleThresholdSeconds: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            minTimeBetweenBackupsSeconds: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            archiveKillswitch: {
                type: string;
                description: string;
            };
            restoreKillswitch: {
                type: string;
                description: string;
            };
        };
    };
    pqcrypto: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            tlsEnableMlkem: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            h3EnableMlkem: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            dtlsWebRTCEnableMlkem: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    dtlsWebRTC: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            tlsVersionDTLS: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    certCompression: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            tlsEnableZlib: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            h3EnableZlib: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            tlsEnableBrotli: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            h3EnableBrotli: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            tlsEnableZstd: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            h3EnableZstd: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    bounceTrackingProtection: {
        description: string;
        owner: string;
        isEarlyStartup: boolean;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            mode: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            requireStatefulBounces: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    remoteTabManagement: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            closeTabsEnabled: {
                description: string;
                type: string;
                fallbackPref: string;
            };
        };
    };
    crlite: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            channel: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            mode: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            timestamps_for_coverage: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    chatbot: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            badgeSidebar: {
                type: string;
                description: string;
            };
            minVersion: {
                type: string;
                description: string;
            };
            prefs: {
                type: string;
                description: string;
            };
        };
    };
    linkPreviews: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            prefs: {
                type: string;
                description: string;
            };
        };
    };
    generatedWidgets: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    resourceMonitor: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    sidebar: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            minVersion: {
                type: string;
                description: string;
            };
            revamp: {
                type: string;
                description: string;
            };
            verticalTabs: {
                type: string;
                description: string;
            };
            visibility: {
                type: string;
                description: string;
            };
        };
    };
    fxms_bmb_button: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        schema: {
            uri: string;
            path: string;
        };
        variables: {};
    };
    certificateTransparency: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            mode: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    setToDefaultPrompt: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            showSpotlightPrompt: {
                description: string;
                type: string;
            };
            message: {
                description: string;
                type: string;
            };
            disablePrompt: {
                description: string;
                type: string;
            };
        };
    };
    tabNotes: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    tabGroups: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    smartTabGroups: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            suggestOtherTabsMethod: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            topicModelRevision: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            embeddingModelRevision: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            nearestNeighborThresholdInt: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            searchTopicEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    tabContextMenu: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            altStructureEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    externalLinkHandling: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            openBehavior: {
                description: string;
                type: string;
                enum: number[];
                fallbackPref: string;
            };
        };
    };
    smartblockEmbeds: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    storageAccessHeuristics: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            popup_past_interaction: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            popup_interaction: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            navigation: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            redirect: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            redirect_tracker: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "partitioned-cookie-attribute": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            chipsMigrationTarget: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            chipsPartitionLimitEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            chipsPartitionLimitDryRun: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            chipsPartitionLimitByteCapacity: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            chipsAffectsTCP: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    trackingProtectionExperimentation: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            deferAnnotationEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    contentClassifier: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            protectionEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            protectionEngines: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            protectionEnginesPbmode: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            annotationEnabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            annotationEngines: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            annotationEnginesPbmode: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            mirrorMode: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            skipURLClassifier: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "auto-pip": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "urlbar-ime-search": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "web-rtc-global-mute-toggles": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    "jpeg-xl": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    windowsUIAutomation: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
                enum: number[];
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    expandSignInButton: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            ctaCopyVariant: {
                description: string;
                type: string;
            };
        };
    };
    contextID: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            rotationPeriodInDays: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            rustBackendEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    deletePrivate: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    fireButton: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            flow: {
                type: string;
                enum: string[];
                fallbackPref: string;
                description: string;
            };
        };
    };
    ipProtection: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            bandwidthMax: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            upgradeNotAvailable: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    webApps: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    defaultWindowsLaunchOnLogin: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    passwordManager: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            reencryptionNeeded: {
                isEarlyStartup: boolean;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    "logins-rust-storage": {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    xpcom: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            low_commit_space_threshold_mb: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            low_commit_space_threshold_percent: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    layout: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            lower_priority_refresh_driver_during_load: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sessionstore_max_tabs_undo: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sessionstore_max_windows_undo: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sessionstore_interval: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            sessionhistory_max_total_viewers: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            smooth_scroll: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            frame_rate: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            frame_rate_throttled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            min_background_timeout_value_without_budget_throttling: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            timeout_throttling_delay: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            initialpaint_delay: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            initialpaint_delay_oopif: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            page_load_deprioritization_period: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    ipc: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            backend_mach: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            process_count: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            process_count_web_isolated: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            preallocated_process_count: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            priority_manager_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            priority_manager_background_grace_period_ms: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            priority_manager_background_perceivable_grace_period_ms: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            priority_manager_background_uses_ecoqos: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    graphics: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            gfx_webrender_all: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            layers_acceleration_force_enabled: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    image: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            image_cache_size: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
            image_mem_decode_bytes_at_a_time: {
                description: string;
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
            };
        };
    };
    walletCustomSchemes: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabledSchemes: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    macAppMenuSetAsDefault: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            shown: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    smartWindow: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                description: string;
            };
            isDefault: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            hiddenOnboardingScreenIds: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            resumeActivity: {
                type: string;
                fallbackPref: string;
                description: string;
            };
            aiTabEnabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            aiTabMenus: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            authImpl: {
                type: string;
                enum: string[];
                description: string;
            };
            guestMode: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                enum: string[];
                description: string;
            };
            smartbarIntent: {
                type: string;
                fallbackPref: string;
                enum: string[];
                description: string;
            };
        };
    };
    smartWindowAgentWatch: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            monitorAnnouncement: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    autoTabGrouping: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            minCandidateTabs: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            maxCandidateTabs: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            minTabsPerGroup: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            maxGroups: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            minCohesion: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            timeoutMs: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            preloadModels: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            supportedLocales: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            thresholdInt: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            nonEnglishThresholdInt: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    contentSharing: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
            serverUrl: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    urlbarShareButton: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    referrals: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    webNotificationsPermissionUi: {
        description: string;
        owner: string;
        hasExposure: boolean;
        exposureDescription: string;
        variables: {
            headline: {
                type: string;
                description: string;
            };
            body: {
                type: string;
                description: string;
            };
            primaryCtaLabel: {
                type: string;
                description: string;
            };
            primaryCtaAccessKey: {
                type: string;
                description: string;
            };
            secondaryCtaLabel: {
                type: string;
                description: string;
            };
            secondaryCtaAccessKey: {
                type: string;
                description: string;
            };
            logoUrl: {
                type: string;
                description: string;
            };
            useSiteFavicon: {
                type: string;
                description: string;
            };
            activationTargeting: {
                type: string;
                description: string;
            };
            headlineL10nId: {
                type: string;
                description: string;
            };
            bodyL10nId: {
                type: string;
                description: string;
            };
            primaryCtaLabelL10nId: {
                type: string;
                description: string;
            };
            secondaryCtaLabelL10nId: {
                type: string;
                description: string;
            };
        };
    };
    editContext: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    nova: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
    trustedTypesReadinessTelemetry: {
        description: string;
        owner: string;
        hasExposure: boolean;
        variables: {
            enabled: {
                type: string;
                setPref: {
                    branch: string;
                    pref: string;
                };
                description: string;
            };
        };
    };
};
