pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "palaceRoot"

    readonly property var backend: logos.module("logos_palace_ui")
    readonly property string roomTitle: backend ? backend.roomTitle : "Connecting..."
    readonly property string roomBackgroundHandle: backend ? backend.roomBackgroundHandle : ""
    readonly property string syncHealth: backend ? backend.syncHealth : "recovering"
    readonly property string deliveryStatus: backend
        ? backend.deliverySessionStatus : "state=unavailable"
    readonly property string storageStatus: backend
        ? backend.storageStatus : "storage=unavailable"
    // Keep degraded and offline states visible in the room chrome. Core owns
    // these values; QML only maps them to compact display labels.
    readonly property string syncDisplayState:
        syncHealth === "fully_synchronized" ? "ok" : syncHealth
    readonly property string storageDisplayState:
        encodedStatusValue(storageStatus, "catalog")
        || encodedStatusValue(storageStatus, "storage")
        || "offline"
    readonly property string assetAuthoringState: backend
        ? backend.assetAuthoringState
        : "{\"version\":1,\"count\":0,\"sessionCount\":0,\"bundleLocked\":false,\"roomAssignments\":{\"atrium\":\"\",\"lounge\":\"\"},\"propAssignment\":null,\"assets\":[]}"
    readonly property string assetAuthoringCapabilityState: backend
        ? backend.assetAuthoringCapabilityState
        : "authority=unavailable;can_author_assets=0;reason=core-unavailable"
    readonly property string activePropAssetState: backend
        ? backend.activePropAsset
        : "{\"version\":1,\"available\":false}"
    readonly property string lezState: backend
        ? backend.lezState : "wallet=closed;ready=0"
    readonly property string identityState: backend
        ? backend.identityState : "identity=none"
    readonly property string palaceState: backend
        ? backend.palaceState : "palace=closed;reason=not-opened"
    readonly property string moderationState: backend
        ? backend.moderationState
        : "state=idle;kind=;action=;target="
    readonly property string moderationCapabilityState: backend
        ? backend.moderationCapabilityState
        : "authority=unavailable;can_ban_user=0;can_ban_prop=0;reason=core-unavailable;checkpoint="
    readonly property string roomLockState: backend
        ? backend.roomLockState
        : "room=;title=;locked=0;can_set_room_lock=0"
    readonly property string spotState: backend
        ? backend.spotState
        : "vm=idle;action=;navigation=0;reason=not-started"
    readonly property string spotActionId: backend
        ? backend.spotActionId : ""
    readonly property string spotReceipt: backend
        ? backend.spotReceipt : ""
    readonly property string participantProjection: backend
        ? backend.participantProjection : "[]"
    readonly property string deliveryNodeEvidence: backend
        ? backend.deliveryNodeEvidence
        : "{\"success\":false,\"reason\":\"node-not-created\"}"
    property string invocationError: ""
    // Local copy of the latest successful watchAction value. Prefer this over
    // backend.lastActionReceipt alone so Gate harnesses observe the returned
    // receipt even if remote-object property propagation lags the sequence bump.
    property string watchedActionReceipt: ""
    property int invocationSequence: 0
    property string acceptanceRoundTripResponse: ""
    readonly property int gateFrameTimingSampleTarget: 120
    readonly property int gateFrameTimingWarmupFrames: 2
    property int gateFrameTimingRequest: 0
    property int gateFrameTimingWarmupsRemaining: 0
    property bool gateFrameTimingRunning: false
    property var gateFrameTimingSamplesUs: []
    property int gateFrameTimingStartFrame: -1
    property int gateFrameTimingEndFrame: -1
    property int gateFrameTimingStartElapsedUs: -1
    property int gateFrameTimingElapsedTimeUs: 0
    property string gateFrameTimingFailure: ""
    readonly property string gateFrameTimingEvidence: JSON.stringify({
        "schema": "logos.palace.frame-animation-timing",
        "version": 1,
        "request": gateFrameTimingRequest,
        "state": gateFrameTimingFailure.length > 0
            ? "failed"
            : (gateFrameTimingRunning
               ? "sampling"
               : (gateFrameTimingRequest === 0
                  ? "idle"
                  : (gateFrameTimingSamplesUs.length
                     === gateFrameTimingSampleTarget
                     ? "complete" : "failed"))),
        "sampleUnit": "microseconds",
        "warmupFrames": gateFrameTimingWarmupFrames,
        "sampleTarget": gateFrameTimingSampleTarget,
        "samplesUs": gateFrameTimingSamplesUs,
        "startFrame": gateFrameTimingStartFrame,
        "endFrame": gateFrameTimingEndFrame,
        "elapsedTimeUs": gateFrameTimingElapsedTimeUs,
        "failure": gateFrameTimingFailure
    })
    readonly property string lastActionReceipt: invocationError.length > 0
        ? invocationError
        : (watchedActionReceipt.length > 0
           ? watchedActionReceipt
           : (backend ? backend.lastActionReceipt : ""))

    // Stable inspector contract for delivery actions and state.
    readonly property string deliveryReceipt: lastActionReceipt
    readonly property int participantCount: participants.length

    // Stable inspector contract used by the compiled Gate 3 harness.
    readonly property string gate3Status: storageStatus
    readonly property string gate3Receipt: lastActionReceipt
    readonly property string gate3RoomHandle: roomBackgroundHandle
    readonly property string gate3AssetAuthoringState:
        assetAuthoringState
    readonly property string gate3ActivePropAsset:
        activePropAssetState
    readonly property string gate3AssetAuthoringEvidence:
        assetAuthoringEvidence(backgroundPreviewEpoch)
    readonly property bool gate3Ready: ready

    // Stable inspector contract used by the compiled Gate 4 harness.
    readonly property string gate4LezState: lezState
    readonly property string gate4IdentityState: identityState
    readonly property string gate4PalaceState: palaceState
    readonly property string gate4Receipt: lastActionReceipt
    readonly property string gate4ModerationState: moderationState
    readonly property string gate4ModerationCapabilityState:
        moderationCapabilityState
    readonly property string gate4ActivePropAsset:
        activePropAssetState
    readonly property bool gate4Ready: ready

    // Stable inspector contract used by the compiled Gate 5 harness.
    readonly property string gate5Action: spotActionId
    readonly property string gate5Status: spotState
    readonly property string gate5RoomTitle: roomTitle
    readonly property string gate5Receipt: invocationError.length > 0
        ? invocationError : spotReceipt
    readonly property bool gate5Ready: ready
    readonly property string gate5VmPhase:
        encodedStatusValue(spotState, "vm")
    readonly property bool gate5DoorBlocked:
        spotActionId.length > 0
        && gate5VmPhase !== "promoted"
        && gate5VmPhase !== "idle"
    readonly property bool palaceOpen:
        encodedStatusValue(palaceState, "palace") === "open"
    readonly property bool entryRoomStateReady:
        encodedStatusValue(palaceState, "entry_state") === "ready"
    readonly property string palaceAuthoritySource:
        encodedStatusValue(palaceState, "authority")
    readonly property bool locallyCommittedAuthority:
        palaceAuthoritySource === "local-committed"
    readonly property bool localDevelopmentProfile:
        encodedStatusValue(lezState, "profile") === "local-development"
    // A local profile deliberately has no public-finality source. Keep that
    // distinction visible without changing what local authority can do.
    readonly property bool localDevelopmentMode:
        locallyCommittedAuthority || localDevelopmentProfile
    // Module readiness keeps the existing automation contract intact. Room
    // controls additionally require a successfully opened Palace and its
    // materialized entry-room state.
    readonly property bool onboardingInitialRoomStatePending:
        onboardingPhase === "preparing-initial-room-state"
        || onboardingPhase === "waiting-initial-room-state"
        || onboardingPhase === "creating-initial-room-state"
        || onboardingPhase === "confirming-initial-room-state"
    readonly property bool roomUsable: ready && palaceOpen
        && entryRoomStateReady && !onboardingInitialRoomStatePending
        && (!onboardingUsesExistingPalace
            || onboardingPhase === "complete")
    readonly property bool onboardingLezReady:
        encodedStatusValue(lezState, "ready") === "1"
    readonly property bool onboardingIdentityReady:
        encodedStatusValue(identityState, "identity") !== "none"
    readonly property bool onboardingResumeReady:
        onboardingLezReady
        && onboardingIdentityReady
        && (onboardingUsesExistingPalace
            || onboardingPalaceTitle.trim().length > 0)
        && (!onboardingUsesExistingPalace
            || (onboardingStorageCatalog.trim().length > 0
                && onboardingStoragePeerEndpoint.trim().length > 0))

    readonly property var participants: parseParticipants(participantProjection)
    readonly property var authoringAssets:
        parseAuthoringAssets(assetAuthoringState)
    readonly property var activePropAsset:
        parseActivePropAsset(activePropAssetState)
    readonly property string availablePropId:
        activePropAsset.available === true
        ? String(activePropAsset.propId) : ""
    readonly property bool canManageAssets: ready
        && encodedStatusValue(
            assetAuthoringCapabilityState, "can_author_assets")
            === "1"
    readonly property string roomSetupPublishReadiness:
        roomSetupPublishReadinessValue()
    // Published creators can copy this bounded catalog into a joiner's
    // onboarding form. It contains CIDs and metadata only; asset bytes stay in
    // Storage and are fetched by the joining Core instance.
    readonly property string sharedStorageCatalog:
        encodedStatusValue(onboardingBundleStatus, "catalog")
    readonly property string sharedStoragePeerEndpoint:
        onboardingStoragePeerEndpoint
    readonly property bool canPublishRoomSetup:
        roomSetupPublishReadiness === "ready"
    // Human moderation is an admin-only LEZ command. Never infer it from
    // display identity or room state; Core derives it from materialized LEZ
    // authority and labels local committed state explicitly.
    readonly property bool canBanUser: ready
        && (encodedStatusValue(moderationCapabilityState, "authority")
                === "finalized"
            || encodedStatusValue(moderationCapabilityState, "authority")
                === "local-committed")
        && encodedStatusValue(moderationCapabilityState, "can_ban_user")
            === "1"
    readonly property bool canBanProp: ready
        && (encodedStatusValue(moderationCapabilityState, "authority")
                === "finalized"
            || encodedStatusValue(moderationCapabilityState, "authority")
                === "local-committed")
        && encodedStatusValue(moderationCapabilityState, "can_ban_prop")
            === "1"
    readonly property bool canSetRoomLock: ready
        && (encodedStatusValue(moderationCapabilityState, "authority")
                === "finalized"
            || encodedStatusValue(moderationCapabilityState, "authority")
                === "local-committed")
        && encodedStatusValue(moderationCapabilityState, "can_set_room_lock")
            === "1"
    readonly property bool canDelegateModerator: ready
        && (encodedStatusValue(moderationCapabilityState, "authority")
                === "finalized"
            || encodedStatusValue(moderationCapabilityState, "authority")
                === "local-committed")
        && encodedStatusValue(moderationCapabilityState,
                              "can_delegate_moderator") === "1"
    readonly property bool roomLocked:
        encodedStatusValue(roomLockState, "locked") === "1"
    readonly property int connectedPeerCount:
        parseConnectedPeerCount(deliveryNodeEvidence)
    property bool ready: false
    property string onboardingPassword: ""
    property string onboardingDisplayName: ""
    property string onboardingPalaceAddress: ""
    // A creator shares this bounded canonical catalog with joiners. It is
    // user-entered data, never a compiled asset manifest or path.
    property string onboardingStorageCatalog: ""
    // Endpoint JSON is copied from the creator's running Storage node. Core
    // validates peer IDs and multiaddrs before dispatching a connection.
    property string onboardingStoragePeerEndpoint: ""
    property string onboardingPalaceTitle: "My Palace"
    property string onboardingPhase: "details"
    property string onboardingError: ""
    property string onboardingReceipt: ""
    property string onboardingFailureStep: ""
    property string onboardingBundleStatus: ""
    property string onboardingCreatedPalaceUri: ""
    property bool onboardingBundlePolling: false
    property bool onboardingBundlePollPending: false
    property bool onboardingFinalityPolling: false
    property bool onboardingFinalityPollPending: false
    property bool onboardingJoinRegistrationPolling: false
    property bool onboardingJoinRegistrationPollPending: false
    property string onboardingJoinRegistrationActionId: ""
    property bool onboardingCreatorActionObserved: false
    property string onboardingCreatorActionId: "0"
    readonly property bool onboardingUsesExistingPalace:
        onboardingPalaceAddress.trim().length > 0
    readonly property bool onboardingWorking:
        onboardingPhase === "starting-lez"
        || onboardingPhase === "creating-identity"
        || onboardingPhase === "connecting-storage"
        || onboardingPhase === "connecting-storage-peer"
        || onboardingPhase === "fetching-storage-catalog"
        || onboardingPhase === "opening-palace"
        || onboardingPhase === "publishing-room-setup"
        || onboardingPhase === "checking-room-setup"
        || onboardingPhase === "creating-palace"
        || onboardingPhase === "confirming-creation"
        || onboardingPhase === "registering-palace-identity"
        || onboardingPhase === "confirming-palace-identity"
        || onboardingPhase === "preparing-initial-room-state"
        || onboardingPhase === "waiting-initial-room-state"
        || onboardingPhase === "creating-initial-room-state"
        || onboardingPhase === "confirming-initial-room-state"
        || onboardingPhase === "opening-created-palace"
    property bool backgroundModerationOpen: false
    property int backgroundPreviewEpoch: 0
    property int backgroundReadyImageCount: 0
    property int backgroundScreenshotFenceRequest: 0
    property bool backgroundScreenshotFenceRunning: false
    property int backgroundScreenshotFenceFrame: -1
    property bool assetImportRunning: false
    readonly property int assetImportMaximumBytes: 10 * 1024 * 1024
    property string assetImportPhase: "idle"
    property int assetImportGeneration: 0
    property string assetImportRequestId: ""
    property string assetImportCapability: ""
    property string assetImportSession: ""
    property string assetImportLabel: ""
    property int assetImportExpectedSequence: 0
    property int assetImportPendingSequence: -1
    property double assetImportStartedAtUnixMs: 0
    property var assetImportTrace: null
    property string gate3AssetImportEvidence: ""
    property string propDraftId: ""
    property string propDraftAnchorX: ""
    property string propDraftAnchorY: ""
    property string propDraftLayer: ""
    property int localMotionX: 5000
    property int localMotionY: 6200
    property string localWornPropId: ""
    // PalaceChat-style prop bag / operator list visibility.
    property bool propBagOpen: false
    property bool userListOpen: false
    property bool roomListOpen: false
    property string selectedModerationUserId: ""
    // Keep whole avatars, their capped speech cards, and the in-scene door
    // inside the clipped room while using the formerly empty lower canvas.
    readonly property int roomCanvasHorizontalInset: 100
    readonly property int roomCanvasTopInset: 104
    readonly property int roomCanvasBottomInset: 166
    readonly property int roomCanvasVerticalInset:
        roomCanvasTopInset + roomCanvasBottomInset
    readonly property int roomCanvasSpeechMaximumHeight: 64
    readonly property int roomCanvasDoorHeight: 72
    readonly property int roomCanvasDoorBottomMargin: 24

    function gateFrameTimingStart(sampleCount) {
        if (sampleCount !== gateFrameTimingSampleTarget
                || gateFrameTimingRunning)
            return -1
        gateFrameTimingFailure = ""
        gateFrameTimingSamplesUs = []
        gateFrameTimingStartFrame = -1
        gateFrameTimingEndFrame = -1
        gateFrameTimingStartElapsedUs = -1
        gateFrameTimingElapsedTimeUs = 0
        gateFrameTimingWarmupsRemaining = gateFrameTimingWarmupFrames
        ++gateFrameTimingRequest
        gateFrameTimingRunning = true
        return gateFrameTimingRequest
    }

    function parseParticipants(encoded) {
        try {
            var decoded = JSON.parse(encoded)
            if (!Array.isArray(decoded))
                return []
            var present = []
            for (var index = 0; index < decoded.length; ++index) {
                if (decoded[index] && decoded[index].present !== false)
                    present.push(decoded[index])
            }
            return present
        } catch (error) {
            return []
        }
    }

    function selectedModerationUser() {
        var selectedUserId = String(selectedModerationUserId)
        for (var index = 0; index < participants.length; ++index) {
            var participant = participants[index] || ({})
            if (String(participant.userId || "") === selectedUserId)
                return participant
        }
        return null
    }

    function selectedModerationUserName() {
        var participant = selectedModerationUser()
        if (!participant)
            return ""
        return String(participant.displayName || participant.userId || "")
    }

    function syncSelectedModerationUser() {
        if (selectedModerationUser() !== null)
            return
        for (var index = 0; index < participants.length; ++index) {
            var participant = participants[index] || ({})
            var participantUserId = String(participant.userId || "")
            if (participantUserId.length > 0) {
                selectedModerationUserId = participantUserId
                return
            }
        }
        selectedModerationUserId = ""
    }

    function focusChatWhenUnobstructed() {
        if (roomUsable && !backgroundModerationOpen && !propBagOpen
                && !roomListOpen && !userListOpen)
            chatInput.forceActiveFocus()
    }

    function closeActiveUtilityPanel() {
        if (backgroundModerationOpen) {
            backgroundModerationOpen = false
            return true
        }
        if (propBagOpen) {
            propBagOpen = false
            return true
        }
        if (roomListOpen) {
            roomListOpen = false
            return true
        }
        if (userListOpen) {
            userListOpen = false
            return true
        }
        return false
    }

    function onboardingInputReady() {
        if (onboardingResumeReady)
            return true
        return onboardingPassword.length > 0
            && onboardingDisplayName.trim().length > 0
            && (onboardingUsesExistingPalace
                || onboardingPalaceTitle.trim().length > 0)
            && (!onboardingUsesExistingPalace
                || (onboardingStorageCatalog.trim().length > 0
                    && onboardingStoragePeerEndpoint.trim().length > 0))
    }

    function clearOnboardingPassword() {
        onboardingPassword = ""
        onboardingPasswordInput.clear()
    }

    function resetOnboardingAfterEdit() {
        onboardingError = ""
        if (onboardingPhase === "error") {
            onboardingPhase = "details"
            onboardingFailureStep = ""
        }
    }

    function onboardingFailureMessage(step, receipt) {
        var result = String(receipt)
        if (result.indexOf("ui-remote-call") >= 0)
            return "Palace could not reach its module. Try again."
        if (step === "lez") {
            if (result.indexOf("lez-invalid-password") >= 0)
                return "Enter a LEZ password with up to 1,024 characters."
            return "Could not start LEZ. Check Logos Control and try again."
        }
        if (step === "identity") {
            if (result.indexOf("identity-invalid-display-name") >= 0)
                return "Enter a display name with up to 48 characters."
            if (result.indexOf("identity-already-exists") >= 0)
                return "This device already has a different display name."
            if (result.indexOf("identity-registration-pending") >= 0)
                return "Your identity is still being confirmed. Try again shortly."
            return "Could not create your identity. Try again."
        }
        if (step === "palace-identity") {
            if (result.indexOf("palace-identity-conflict") >= 0)
                return "This identity is already registered with a different key."
            return "Could not register your identity in this Palace. Try again."
        }
        if (step === "palace") {
            if (result.indexOf("invalid-palace-uri") >= 0)
                return "Enter the full Palace address, starting with palace://."
            if (result.indexOf("palace-history-busy") >= 0)
                return "This Palace is still opening. Please wait."
            return "Could not open this Palace. Check the address and try again."
        }
        if (step === "storage") {
            if (result.indexOf("storage-not-running") >= 0)
                return "Start Storage in Logos Control, then try again."
            return "Could not connect to Storage. Check Logos Control and try again."
        }
        if (step === "storage-peer")
            return "Paste the creator's Storage peer endpoint to fetch room assets."
        if (step === "catalog") {
            if (result.indexOf("storage-catalog-required") >= 0)
                return "Paste the shared room catalog to join this Palace."
            if (result.indexOf("storage-catalog-invalid") >= 0)
                return "The shared room catalog is invalid or incomplete. Paste it again."
            return "Could not fetch the room assets. Check Storage peers and try again."
        }
        if (step === "bundle") {
            if (result.indexOf("storage-not-running") >= 0)
                return "Storage is not ready to publish the room setup."
            return "Could not publish the room setup. Check both backgrounds and try again."
        }
        if (step === "create") {
            if (result.indexOf("palace-storage-graph-not-ready") >= 0)
                return "Room setup is still being prepared. Please wait and try again."
            return "Could not create the Palace. Try again."
        }
        if (step === "finality")
            return "The Palace creation is still being confirmed. Try again shortly."
        if (step === "created-palace")
            return "The Palace was created but could not be opened yet. Try again."
        return "Could not complete setup. Try again."
    }

    function onboardingRecordSuccess(receipt) {
        onboardingReceipt = String(receipt)
        invocationError = ""
        watchedActionReceipt = onboardingReceipt
        ++invocationSequence
    }

    function onboardingFail(step, cause) {
        onboardingBundlePolling = false
        onboardingBundlePollPending = false
        onboardingFinalityPolling = false
        onboardingFinalityPollPending = false
        onboardingJoinRegistrationPolling = false
        onboardingJoinRegistrationPollPending = false
        onboardingPhase = "error"
        onboardingFailureStep = step
        onboardingError = onboardingFailureMessage(step, cause)
        onboardingReceipt = ""
        invocationError = String(cause)
        watchedActionReceipt = ""
        ++invocationSequence
    }

    function invokeOnboardingStep(phase, step, pendingCall, next) {
        onboardingPhase = phase
        logos.watch(pendingCall, function (value) {
            var receipt = String(value)
            if (receipt.indexOf("rejected=") === 0) {
                onboardingFail(step, receipt)
                return
            }
            onboardingRecordSuccess(receipt)
            next(receipt)
        }, function (error) {
            onboardingFail(step, "rejected=ui-remote-call;" + String(error))
        })
    }

    function finishOnboardingOpen(receipt, waitingPhase, failureStep) {
        var state = encodedStatusValue(receipt, "palace")
        if (failureStep === "created-palace"
                && retryableCreatedPalaceOpenState(receipt)) {
            onboardingPhase = waitingPhase
            onboardingError = ""
            if (!onboardingCreatedPalaceRetryTimer.running)
                onboardingCreatedPalaceRetryTimer.start()
            return
        }
        if (state === "rejected" || state === "degraded") {
            onboardingFail(failureStep, receipt)
            return
        }
        if (palaceOpen && failureStep === "palace"
                && onboardingUsesExistingPalace) {
            beginExistingPalaceRegistration()
            return
        }
        onboardingPhase = palaceOpen ? "complete" : waitingPhase
        if (palaceOpen)
            clearOnboardingPassword()
    }

    function openExistingPalaceAfterCatalog() {
        invokeOnboardingStep(
            "opening-palace", "palace",
            backend.openPalace(onboardingPalaceAddress.trim()),
            function (receipt) {
                finishOnboardingOpen(receipt, "waiting-palace", "palace")
            })
    }

    function finishExistingPalaceRegistration() {
        onboardingJoinRegistrationPolling = false
        onboardingJoinRegistrationPollPending = false
        onboardingJoinRegistrationActionId = ""
        onboardingPhase = "complete"
        onboardingError = ""
        clearOnboardingPassword()
    }

    function updateExistingPalaceRegistration(receipt) {
        var status = String(receipt)
        if (status.indexOf("rejected=") === 0) {
            onboardingFail("palace-identity", status)
            return
        }
        if (encodedStatusValue(status, "registration") === "already") {
            finishExistingPalaceRegistration()
            return
        }
        var actionId = encodedStatusValue(status, "action")
        if (!/^[1-9][0-9]*$/.test(actionId)) {
            onboardingFail(
                "palace-identity",
                "rejected=palace-identity-action-missing")
            return
        }
        onboardingJoinRegistrationActionId = actionId
        onboardingPhase = "confirming-palace-identity"
        onboardingJoinRegistrationPolling = true
    }

    function beginExistingPalaceRegistration() {
        if (!onboardingUsesExistingPalace || !palaceOpen
                || onboardingPhase === "complete"
                || onboardingJoinRegistrationPolling)
            return
        onboardingPhase = "registering-palace-identity"
        watchAction(
            backend.registerPalaceUser(),
            updateExistingPalaceRegistration,
            function (receipt) {
                if (retryableCreatorActionRejection(receipt)) {
                    onboardingPhase = "confirming-palace-identity"
                    onboardingError = ""
                    onboardingJoinRegistrationPolling = true
                    return
                }
                onboardingFail("palace-identity", receipt)
            })
    }

    function pollExistingPalaceRegistration() {
        if (!onboardingJoinRegistrationPolling
                || onboardingJoinRegistrationPollPending
                || !ready || !backend)
            return
        onboardingJoinRegistrationPollPending = true
        watchAction(
            backend.registerPalaceUser(),
            function (receipt) {
                if (encodedStatusValue(receipt, "registration")
                        === "already") {
                    onboardingJoinRegistrationPollPending = false
                    finishExistingPalaceRegistration()
                    return
                }
                var actionId = encodedStatusValue(receipt, "action")
                if (!/^[1-9][0-9]*$/.test(actionId)) {
                    onboardingJoinRegistrationPollPending = false
                    onboardingFail(
                        "palace-identity",
                        "rejected=palace-identity-action-missing")
                    return
                }
                onboardingJoinRegistrationActionId = actionId
                onboardingPhase = "confirming-palace-identity"
                watchAction(
                    backend.reconcilePalaceTransition(actionId),
                    function () {
                        onboardingJoinRegistrationPollPending = false
                    },
                    function (rejection) {
                        onboardingJoinRegistrationPollPending = false
                        if (!retryableCreatorActionRejection(rejection))
                            onboardingFail("palace-identity", rejection)
                    })
            },
            function (receipt) {
                onboardingJoinRegistrationPollPending = false
                if (retryableCreatorActionRejection(receipt)) {
                    onboardingPhase = "confirming-palace-identity"
                    onboardingError = ""
                    onboardingReceipt = String(receipt)
                    onboardingJoinRegistrationPolling = true
                    return
                }
                onboardingFail("palace-identity", receipt)
            })
    }

    function parseStoragePeerEndpoint(encoded) {
        var value = String(encoded || "").trim()
        if (value.indexOf("ok;") === 0)
            value = value.slice(3)
        try {
            var endpoint = JSON.parse(value)
            var peerId = String(endpoint.peerId || "")
            var addresses = Array.isArray(endpoint.addrs)
                ? endpoint.addrs
                : (Array.isArray(endpoint.announceAddresses)
                   ? endpoint.announceAddresses : [])
            if (peerId.length === 0 || addresses.length === 0)
                return null
            return {
                peerId: peerId,
                addressesJson: JSON.stringify(addresses)
            }
        } catch (error) {
            return null
        }
    }

    function refreshSharedStoragePeerEndpoint() {
        watchAction(backend.storagePeerEndpoint(), function (receipt) {
            if (String(receipt).indexOf("rejected=") !== 0)
                onboardingStoragePeerEndpoint = String(receipt)
        }, null)
    }

    function updateExistingStorageCatalog(receipt) {
        var status = String(receipt)
        onboardingBundleStatus = status
        if (status.indexOf("rejected=") === 0) {
            onboardingFail("catalog", status)
            return
        }
        var state = encodedStatusValue(status, "state")
        if (state === "degraded") {
            onboardingFail("catalog", status)
            return
        }
        if (state === "verified" || state === "retained") {
            onboardingBundlePolling = false
            onboardingBundlePollPending = false
            openExistingPalaceAfterCatalog()
        }
    }

    function openExistingPalace() {
        if (onboardingStorageCatalog.trim().length === 0) {
            onboardingFail("catalog", "rejected=storage-catalog-required")
            return
        }
        var peer = parseStoragePeerEndpoint(onboardingStoragePeerEndpoint)
        if (peer === null) {
            onboardingFail("storage-peer", "rejected=storage-peer-endpoint-required")
            return
        }
        invokeOnboardingStep(
            "connecting-storage", "storage",
            backend.connectStorage(),
            function () {
                invokeOnboardingStep(
                    "connecting-storage-peer", "storage-peer",
                    backend.connectStoragePeer(
                        peer.peerId, peer.addressesJson),
                    function () {
                        invokeOnboardingStep(
                            "fetching-storage-catalog", "catalog",
                            backend.fetchMvpStorageBundle(
                                onboardingStorageCatalog.trim()),
                            function (receipt) {
                                onboardingBundleStatus = receipt
                                onboardingBundlePolling = true
                                pollRoomSetupPublication()
                            })
                    })
            })
    }

    function enterCreatorMode() {
        clearOnboardingPassword()
        onboardingFailureStep = ""
        onboardingError = ""
        onboardingPhase = "authoring-rooms"
        backgroundModerationOpen = true
    }

    function startOnboarding() {
        if (!ready || !backend) {
            onboardingPhase = "error"
            onboardingError =
                "Palace is still starting. Try again in a moment."
            return "rejected=ui-not-ready"
        }
        if (onboardingResumeReady) {
            onboardingError = ""
            onboardingReceipt = ""
            onboardingFailureStep = ""
            if (onboardingUsesExistingPalace)
                openExistingPalace()
            else
                enterCreatorMode()
            return "pending"
        }
        if (!onboardingInputReady()) {
            onboardingPhase = "details"
            onboardingError =
                "Enter a LEZ password and display name, then choose a Palace address or title."
            return "rejected=onboarding-details-required"
        }

        onboardingError = ""
        onboardingReceipt = ""
        onboardingFailureStep = ""
        invokeOnboardingStep(
            "starting-lez", "lez",
            backend.startLez(String(onboardingPassword)),
            function () {
                invokeOnboardingStep(
                    "creating-identity", "identity",
                    backend.createIdentity(
                        onboardingDisplayName.trim()),
                    function () {
                        if (onboardingUsesExistingPalace)
                            openExistingPalace()
                        else
                            enterCreatorMode()
                    })
            })
        return "pending"
    }

    function publishRoomSetup() {
        if (onboardingPhase !== "authoring-rooms")
            return gate3PublishBundle()
        if (!canPublishRoomSetup) {
            onboardingError = roomSetupPublishMessage()
            return "rejected=room-setup-not-ready"
        }
        onboardingError = ""
        onboardingPhase = "publishing-room-setup"
        backgroundModerationOpen = false
        return publishRoomSetupBundle(function (receipt) {
            onboardingBundleStatus = receipt
            onboardingPhase = "checking-room-setup"
            onboardingBundlePolling = true
            pollRoomSetupPublication()
        }, function (receipt) {
            onboardingFail("bundle", receipt)
        })
    }

    function updateRoomSetupPublication(receipt) {
        var status = String(receipt)
        onboardingBundleStatus = status
        if (status.indexOf("rejected=") === 0) {
            onboardingFail("bundle", status)
            return
        }
        var state = encodedStatusValue(status, "state")
        var catalog = encodedStatusValue(status, "catalog")
        if (state === "degraded") {
            onboardingFail("bundle", status)
            return
        }
        if ((state === "verified" || state === "retained")
                && catalog.length > 0) {
            onboardingBundlePolling = false
            onboardingBundlePollPending = false
            refreshSharedStoragePeerEndpoint()
            createOnboardingPalace()
        }
    }

    function pollRoomSetupPublication() {
        if (!onboardingBundlePolling || onboardingBundlePollPending
                || !ready || !backend) {
            return
        }
        onboardingBundlePollPending = true
        watchAction(backend.mvpStorageBundleStatus(), function (receipt) {
            onboardingBundlePollPending = false
            if (onboardingUsesExistingPalace)
                updateExistingStorageCatalog(receipt)
            else
                updateRoomSetupPublication(receipt)
        }, function (receipt) {
            onboardingBundlePollPending = false
            onboardingFail(onboardingUsesExistingPalace ? "catalog" : "bundle", receipt)
        })
    }

    function createOnboardingPalace() {
        var title = onboardingPalaceTitle.trim()
        if (title.length === 0) {
            onboardingFail("create", "rejected=palace-title-empty")
            return "rejected=palace-title-empty"
        }
        onboardingPhase = "creating-palace"
        onboardingError = ""
        return watchAction(backend.createPalace(title), function (receipt) {
            var palaceUri = encodedStatusValue(receipt, "palace_uri")
            if (!/^palace:\/\/[0-9a-f]{64}$/.test(palaceUri)) {
                onboardingFail("create", "rejected=palace-uri-missing")
                return
            }
            onboardingCreatedPalaceUri = palaceUri
            onboardingCreatorActionId = "0"
            onboardingCreatorActionObserved =
                encodedStatusValue(receipt, "durable") === "observed"
            onboardingPhase = "confirming-creation"
            onboardingFinalityPolling = true
            pollCreatorAction()
        }, function (receipt) {
            if (retryableCreatorActionRejection(receipt)) {
                onboardingPhase = "creating-palace"
                onboardingError = ""
                onboardingReceipt = String(receipt)
                if (!onboardingCreatedPalaceRetryTimer.running)
                    onboardingCreatedPalaceRetryTimer.start()
                return
            }
            onboardingFail("create", receipt)
        })
    }

    function updateCreatorAction(receipt) {
        var status = String(receipt)
        if (status.indexOf("rejected=") === 0) {
            updateCreatorActionRejection(status)
            return
        }
        var durable = encodedStatusValue(status, "durable")
        if (durable === "finalized"
                || encodedStatusValue(status, "completion")
                    === "local-committed") {
            onboardingFinalityPolling = false
            onboardingFinalityPollPending = false
            if (onboardingCreatorActionId === "0") {
                prepareInitialRoomState()
                return
            }
            if (onboardingCreatorActionId !== "0") {
                openCreatedPalace()
                return
            }
            onboardingFail("finality", status)
            return
        }
        if (durable === "observed")
            onboardingCreatorActionObserved = true
        else if (durable !== "submitted_to_lez") {
            onboardingFail("finality", status)
        }
    }

    // A submitted LEZ action can briefly be absent from a stable account
    // snapshot while the sequencer applies it. Keep the creation flow alive
    // for only those transport/visibility races; malformed or mismatched
    // authority data remains a terminal user-visible failure.
    function retryableCreatorActionRejection(receipt) {
        var status = String(receipt)
        if (status === "rejected=palace-identity-registration-pending")
            return true
        return /^rejected=lez-stable-account-read;reason=(sync-[^;]+|height-before|height-after|wallet-height-raced|account-[0-9]+)$/.test(status)
            || /^rejected=lez-observation;reason=(transaction-not-materialized|invalid-account-response|invalid-account-field|invalid-account-data|invalid-record|unexpected-observation-record|observation-mismatch|unstable-height)$/.test(status)
            || status === "rejected=lez-root-transaction-pending"
            || status === "rejected=lez-authority-history-rebuild-required"
    }

    function retryableCreatedPalaceOpenState(receipt) {
        var status = String(receipt)
        return encodedStatusValue(status, "palace") === "rejected"
            && encodedStatusValue(status, "reason")
                === "local-committed-initialize-not-found"
    }

    function updateCreatorActionRejection(receipt) {
        if (!retryableCreatorActionRejection(receipt)) {
            onboardingFail("finality", receipt)
            return
        }
        // Retain a narrowly scoped diagnostic without exposing it as a setup
        // failure. The active poll will either observe the action or return a
        // non-retryable reason to the user.
        onboardingReceipt = String(receipt)
        invocationError = ""
        watchedActionReceipt = onboardingReceipt
    }

    function pollCreatorAction() {
        if (!onboardingFinalityPolling || onboardingFinalityPollPending
                || !ready || !backend) {
            return
        }
        onboardingFinalityPollPending = true
        if (!onboardingCreatorActionObserved) {
            observeCreatorAction(onboardingCreatorActionId, function (receipt) {
                onboardingFinalityPollPending = false
                updateCreatorAction(receipt)
            }, function (receipt) {
                onboardingFinalityPollPending = false
                updateCreatorActionRejection(receipt)
            })
            return
        }
        reconcileCreatorAction(onboardingCreatorActionId, function (receipt) {
            onboardingFinalityPollPending = false
            updateCreatorAction(receipt)
        }, function (receipt) {
            onboardingFinalityPollPending = false
            updateCreatorActionRejection(receipt)
        })
    }

    function openCreatedPalace() {
        if (!/^palace:\/\/[0-9a-f]{64}$/.test(
                onboardingCreatedPalaceUri)) {
            onboardingFail("created-palace", "rejected=palace-uri-missing")
            return "rejected=palace-uri-missing"
        }
        onboardingPhase = "opening-created-palace"
        return watchAction(
            backend.openPalace(onboardingCreatedPalaceUri),
            function (receipt) {
                finishOnboardingOpen(
                    receipt, "waiting-created-palace", "created-palace")
            }, function (receipt) {
                onboardingFail("created-palace", receipt)
            })
    }

    function prepareInitialRoomState() {
        if (!/^palace:\/\/[0-9a-f]{64}$/.test(
                onboardingCreatedPalaceUri)) {
            onboardingFail("created-palace", "rejected=palace-uri-missing")
            return "rejected=palace-uri-missing"
        }
        onboardingPhase = "preparing-initial-room-state"
        return watchAction(
            backend.openPalace(onboardingCreatedPalaceUri),
            function (receipt) {
                continueInitialRoomStatePreparation(receipt)
            }, function (receipt) {
                onboardingFail("finality", receipt)
            })
    }

    function continueInitialRoomStatePreparation(receipt) {
        var state = encodedStatusValue(receipt, "palace")
        if (state === "rejected" || state === "degraded") {
            onboardingFail("finality", receipt)
            return
        }
        if (state !== "open") {
            onboardingPhase = "waiting-initial-room-state"
            if (!onboardingCreatedPalaceRetryTimer.running)
                onboardingCreatedPalaceRetryTimer.start()
            return
        }
        createInitialRoomState()
    }

    function createInitialRoomState() {
        onboardingPhase = "creating-initial-room-state"
        return watchAction(backend.createInitialRoomState(),
            function (receipt) {
                var status = String(receipt)
                if (encodedStatusValue(status, "initial_room_state")
                        === "ready") {
                    openCreatedPalace()
                    return
                }
                var actionId = encodedStatusValue(status, "action")
                if (!/^[1-9][0-9]*$/.test(actionId)) {
                    onboardingFail(
                        "finality", "rejected=initial-room-state-action")
                    return
                }
                onboardingCreatorActionId = actionId
                if (encodedStatusValue(status, "durable")
                        === "finalized") {
                    openCreatedPalace()
                    return
                }
                onboardingCreatorActionObserved =
                    encodedStatusValue(status, "durable") === "observed"
                onboardingPhase = "confirming-initial-room-state"
                onboardingFinalityPolling = true
                pollCreatorAction()
            }, function (receipt) {
                onboardingFail("finality", receipt)
            })
    }

    function activateOnboarding() {
        if (onboardingPhase === "authoring-rooms") {
            backgroundModerationOpen = true
            return "pending"
        }
        if (onboardingPhase === "error") {
            if (onboardingFailureStep === "bundle") {
                onboardingPhase = "authoring-rooms"
                backgroundModerationOpen = true
                return "pending"
            }
            if (onboardingFailureStep === "create")
                return createOnboardingPalace()
            if (onboardingFailureStep === "finality") {
                onboardingPhase = onboardingCreatorActionId === "0"
                    ? "confirming-creation"
                    : "confirming-initial-room-state"
                onboardingFinalityPolling = true
                pollCreatorAction()
                return "pending"
            }
            if (onboardingFailureStep === "created-palace")
                return openCreatedPalace()
            if (onboardingFailureStep === "palace-identity") {
                onboardingPhase = "confirming-palace-identity"
                onboardingJoinRegistrationPolling = true
                pollExistingPalaceRegistration()
                return "pending"
            }
        }
        return startOnboarding()
    }

    function onboardingProgressText() {
        if (!ready)
            return "Connecting to Logos Control…"
        if (onboardingPhase === "starting-lez")
            return "Starting LEZ…"
        if (onboardingPhase === "creating-identity")
            return "Creating your identity…"
        if (onboardingPhase === "connecting-storage")
            return "Connecting to Storage…"
        if (onboardingPhase === "connecting-storage-peer")
            return "Connecting to the Palace Storage peer…"
        if (onboardingPhase === "fetching-storage-catalog")
            return "Fetching the room assets…"
        if (onboardingPhase === "opening-palace")
            return "Opening the Palace…"
        if (onboardingPhase === "authoring-rooms")
            return "Choose Atrium and Lounge backgrounds in Assets, then publish the room setup."
        if (onboardingPhase === "publishing-room-setup"
                || onboardingPhase === "checking-room-setup")
            return "Publishing and checking the room setup…"
        if (onboardingPhase === "creating-palace")
            return "Creating the Palace…"
        if (onboardingPhase === "confirming-creation"
                && retryableCreatorActionRejection(onboardingReceipt)) {
            return "Waiting for LEZ to expose the new Palace…"
        }
        if (onboardingPhase === "confirming-creation")
            return "Confirming the Palace creation…"
        if (onboardingPhase === "registering-palace-identity")
            return "Registering your identity in this Palace…"
        if (onboardingPhase === "confirming-palace-identity")
            return "Confirming your identity in this Palace…"
        if (onboardingPhase === "preparing-initial-room-state"
                || onboardingPhase === "waiting-initial-room-state")
            return "Preparing the first room…"
        if (onboardingPhase === "creating-initial-room-state"
                || onboardingPhase === "confirming-initial-room-state")
            return "Preparing the first room…"
        if (onboardingPhase === "opening-created-palace")
            return "Opening your new Palace…"
        if (onboardingPhase === "waiting-palace") {
            if (encodedStatusValue(palaceState, "palace") === "scanning")
                return "Confirming the Palace details…"
            return "Waiting for the Palace to open…"
        }
        if (onboardingPhase === "waiting-created-palace") {
            if (encodedStatusValue(palaceState, "palace") === "scanning")
                return "Confirming your new Palace…"
            return "Waiting for your new Palace to open…"
        }
        if (onboardingPhase === "error")
            return onboardingError
        return "Open an existing Palace or create a new one."
    }

    function parseAuthoringAssets(encoded) {
        try {
            var decoded = JSON.parse(encoded)
            if (!decoded || decoded.version !== 1
                    || !Array.isArray(decoded.assets)
                    || decoded.count !== decoded.assets.length)
                return []
            return decoded.assets
        } catch (error) {
            return []
        }
    }

    function parseActivePropAsset(encoded) {
        try {
            var decoded = JSON.parse(encoded)
            if (!decoded || decoded.version !== 1
                    || decoded.available !== true
                    || !/^[a-z][a-z0-9_-]{0,63}$/.test(
                        String(decoded.propId || ""))
                    || !/^[0-9a-f]{64}$/.test(
                        String(decoded.handle || ""))
                    || String(decoded.contentSha256 || "")
                        !== String(decoded.handle)
                    || !Number.isInteger(decoded.width)
                    || !Number.isInteger(decoded.height)
                    || decoded.width <= 0 || decoded.height <= 0
                    || !Number.isInteger(decoded.anchorX)
                    || !Number.isInteger(decoded.anchorY)
                    || decoded.anchorX < 0 || decoded.anchorY < 0
                    || decoded.anchorX >= decoded.width
                    || decoded.anchorY >= decoded.height
                    || !/^[a-z][a-z0-9_-]{0,63}$/.test(
                        String(decoded.layer || ""))) {
                return {
                    "version": 1,
                    "available": false
                }
            }
            return decoded
        } catch (error) {
            return {
                "version": 1,
                "available": false
            }
        }
    }

    function validAssetIdentifier(value) {
        return /^[a-z][a-z0-9_-]{0,63}$/.test(
            String(value).trim())
    }

    function parsedAssetAnchor(value) {
        var encoded = String(value).trim()
        if (!/^(0|[1-9][0-9]{0,9})$/.test(encoded))
            return -1
        var parsed = Number(encoded)
        return Number.isSafeInteger(parsed) ? parsed : -1
    }

    function validPropLayer(value) {
        var normalized = String(value).trim()
        return normalized === "head"
            || normalized === "body"
            || normalized === "hand"
            || normalized === "back"
    }

    function propDraftReady() {
        return validAssetIdentifier(propDraftId)
            && validPropLayer(propDraftLayer)
            && parsedAssetAnchor(propDraftAnchorX) >= 0
            && parsedAssetAnchor(propDraftAnchorY) >= 0
    }

    function clampAnchorToAsset(value, sourceExtent) {
        var extent = Math.max(1, Number(sourceExtent))
        var coordinate = Math.round(Number(value))
        if (isNaN(coordinate) || !isFinite(coordinate))
            return 0
        return Math.max(0, Math.min(extent - 1, coordinate))
    }

    function propPreviewScale(sourceWidth, sourceHeight) {
        return Math.min(
            1,
            92 / Math.max(1, Number(sourceWidth)),
            62 / Math.max(1, Number(sourceHeight)))
    }

    function propPreviewTargetX(layer, stageWidth) {
        var normalized = String(layer)
        if (normalized === "hand")
            return stageWidth - 22
        if (normalized === "back")
            return 34
        return Math.round(stageWidth / 2)
    }

    function propPreviewTargetY(layer, stageHeight) {
        var normalized = String(layer)
        if (normalized === "head")
            return 18
        if (normalized === "body")
            return Math.round(stageHeight / 2) - 2
        return Math.round(stageHeight * 0.67)
    }

    function setPropDraftAnchorFromPreview(
            localX, localY, scale, sourceWidth, sourceHeight) {
        if (!canManageAssets || scale <= 0)
            return
        propDraftAnchorX = String(clampAnchorToAsset(
            Number(localX) / scale, sourceWidth))
        propDraftAnchorY = String(clampAnchorToAsset(
            Number(localY) / scale, sourceHeight))
    }

    function assetAuthoringEvidence(epoch) {
        var published = 0
        for (var assetIndex = 0;
             assetIndex < authoringAssets.length; ++assetIndex) {
            if (authoringAssets[assetIndex].reviewState === "approved"
                    && authoringAssets[assetIndex].publicationState
                        === "published"
                    && String(
                        authoringAssets[assetIndex].cid).length > 0)
                ++published
        }
        var roomAssignments = {"atrium": "", "lounge": ""}
        var propAssigned = false
        try {
            var state = JSON.parse(assetAuthoringState)
            if (state && state.roomAssignments)
                roomAssignments = state.roomAssignments
            propAssigned = Boolean(state && state.propAssignment
                                   && String(
                                       state.propAssignment.handle).length
                                      === 64)
        } catch (error) {
        }
        // cardCount must track the authoring model, not GridView.count.
        // palaceBackgroundGrid is a Flickable+Flow+Repeater so that every
        // Set Atrium/Lounge control stays mounted; Flickable has no count.
        return JSON.stringify({
            "schema": "logos.palace.asset-authoring-render",
            "version": 1,
            "open": backgroundModerationOpen,
            "cardCount": authoringAssets.length,
            "readyImageCount": backgroundReadyImageCount,
            "publishedCount": published,
            "atriumAssigned":
                String(roomAssignments.atrium || "").length === 64,
            "loungeAssigned":
                String(roomAssignments.lounge || "").length === 64,
            "propAssigned": propAssigned,
            "fenceRequest": backgroundScreenshotFenceRequest,
            "fenceState": backgroundScreenshotFenceRunning
                ? "waiting"
                : (backgroundScreenshotFenceRequest === 0
                   ? "idle" : "complete"),
            "fenceFrame": backgroundScreenshotFenceFrame,
            "epoch": epoch
        })
    }

    function gate3AssetScreenshotFenceStart() {
        if (!backgroundModerationOpen
                || backgroundScreenshotFenceRunning)
            return -1
        backgroundScreenshotFenceFrame = -1
        ++backgroundScreenshotFenceRequest
        backgroundScreenshotFenceRunning = true
        return backgroundScreenshotFenceRequest
    }

    // Authoring preview counters — keep transitions bounded so open/close
    // cycles cannot accumulate readyImageCount (see tests/palace_ui_lifecycle.mjs).
    function noteAuthoringPreviewReady(wasCounted) {
        if (wasCounted)
            return true
        backgroundReadyImageCount =
            Math.max(0, backgroundReadyImageCount) + 1
        return true
    }

    function noteAuthoringPreviewLost(wasCounted) {
        if (!wasCounted)
            return false
        backgroundReadyImageCount =
            Math.max(0, backgroundReadyImageCount - 1)
        return false
    }

    function resetAuthoringPreviewState() {
        backgroundReadyImageCount = 0
        backgroundScreenshotFenceRunning = false
        backgroundScreenshotFenceFrame = -1
        ++backgroundPreviewEpoch
    }

    function parseConnectedPeerCount(encoded) {
        try {
            var evidence = JSON.parse(encoded)
            if (!evidence || evidence.success !== true)
                return 0
            var peers = evidence.connectedPeers
            if (Array.isArray(peers))
                return peers.length
            if (!peers || typeof peers !== "object")
                return 0
            var count = 0
            for (var peerId in peers) {
                if (Object.prototype.hasOwnProperty.call(peers, peerId))
                    ++count
            }
            return count
        } catch (error) {
            return 0
        }
    }

    function encodedStatusValue(encoded, name) {
        var fields = String(encoded).split(";")
        var prefix = name + "="
        for (var index = 0; index < fields.length; ++index) {
            if (fields[index].indexOf(prefix) === 0)
                return fields[index].slice(prefix.length)
        }
        return ""
    }

    function statusValue(name) {
        return encodedStatusValue(deliveryStatus, name)
    }

    function clampCoordinate(value) {
        var coordinate = Number(value)
        if (isNaN(coordinate) || !isFinite(coordinate))
            return 0
        return Math.max(0, Math.min(10000, Math.round(coordinate)))
    }

    function protocolCoordinate(value, fallbackIndex, count) {
        if (value === null || value === undefined || value === "") {
            return Math.round((fallbackIndex + 1) * 10000
                              / Math.max(2, count + 1))
        }
        var coordinate = Number(value)
        if (isNaN(coordinate) || !isFinite(coordinate))
            return 5000
        return clampCoordinate(coordinate)
    }

    function roomX(coordinate) {
        var usableWidth = Math.max(
            1, roomCanvas.width - roomCanvasHorizontalInset * 2)
        return roomCanvasHorizontalInset
            + clampCoordinate(coordinate) * usableWidth / 10000
    }

    function roomY(coordinate) {
        var usableHeight = Math.max(
            1, roomCanvas.height - roomCanvasVerticalInset)
        return roomCanvasTopInset
            + clampCoordinate(coordinate) * usableHeight / 10000
    }

    // Inverse of roomX/roomY. Keep pointer motion in the same bounded
    // protocol coordinates as remote participant projections.
    function canvasPixelToProtocol(pixel, inset, usableSpan) {
        var canvasPixel = Number(pixel)
        if (isNaN(canvasPixel) || !isFinite(canvasPixel))
            return 0
        var span = Math.max(1, Number(usableSpan))
        return clampCoordinate(
            (canvasPixel - Number(inset)) * 10000 / span)
    }

    function canvasPixelsToProtocol(pixelX, pixelY) {
        return {
            "x": canvasPixelToProtocol(
                pixelX, roomCanvasHorizontalInset,
                Math.max(1, roomCanvas.width
                         - roomCanvasHorizontalInset * 2)),
            "y": canvasPixelToProtocol(
                pixelY, roomCanvasTopInset,
                Math.max(1, roomCanvas.height - roomCanvasVerticalInset))
        }
    }

    function rejectedNotReady() {
        invocationError = "rejected=ui-not-ready"
        watchedActionReceipt = ""
        ++invocationSequence
        return invocationError
    }

    function watchAction(pendingCall, onAccepted, onRejected) {
        logos.watch(pendingCall, function (value) {
            var receipt = String(value)
            // Surface terminal rejections through the local sequence so
            // moderation waits can fail closed. Success clears any prior
            // local rejection and may run the optional accepted callback.
            if (receipt.indexOf("rejected=") === 0) {
                invocationError = receipt
                watchedActionReceipt = ""
                if (onRejected)
                    onRejected(receipt)
            } else {
                invocationError = ""
                watchedActionReceipt = receipt
                if (onAccepted)
                    onAccepted(receipt)
            }
            ++invocationSequence
        }, function (error) {
            invocationError = "rejected=ui-remote-call;" + String(error)
            watchedActionReceipt = ""
            if (onRejected)
                onRejected(invocationError)
            ++invocationSequence
        })
        return "pending"
    }

    function acceptanceApplicationRoundTrip(payload) {
        if (!ready || !backend) {
            acceptanceRoundTripResponse = rejectedNotReady()
            return acceptanceRoundTripResponse
        }
        invocationError = ""
        logos.watch(
            backend.applicationRoundTrip(String(payload)),
            function (value) {
                acceptanceRoundTripResponse = String(value)
                ++invocationSequence
            },
            function (error) {
                acceptanceRoundTripResponse =
                    "rejected=ui-remote-call;" + String(error)
                invocationError = acceptanceRoundTripResponse
                ++invocationSequence
            })
        return "pending"
    }

    function startDelivery(configJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startDelivery(String(configJson)), null)
    }

    function gate1EnterRoom(roomId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.enterRoom(String(roomId)), null)
    }

    // Bounded room palette for MVP. Lounge ingress remains a program-driven
    // door transition; returning to Atrium keeps the existing direct action.
    function selectFixedRoom(roomId) {
        if (!ready || !backend)
            return rejectedNotReady()
        var selectedRoom = String(roomId).toLowerCase()
        if (selectedRoom !== "atrium" && selectedRoom !== "lounge") {
            invocationError = "rejected=room-not-listed"
            watchedActionReceipt = ""
            ++invocationSequence
            return invocationError
        }
        var currentRoom = String(roomTitle).toLowerCase()
        if (selectedRoom === currentRoom) {
            invocationError = ""
            watchedActionReceipt = "ok=room-current;room=" + selectedRoom
            ++invocationSequence
            return watchedActionReceipt
        }
        if (selectedRoom === "lounge" && currentRoom === "atrium")
            return root.gate5UseDoor()
        if (selectedRoom === "atrium" && currentRoom === "lounge")
            return watchAction(backend.enterRoom("atrium"), null)
        invocationError = "rejected=room-transition-unavailable"
        watchedActionReceipt = ""
        ++invocationSequence
        return invocationError
    }

    function sendSpeech(text) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.say(String(text)), null)
    }

    function moveAvatar(x, y) {
        if (!ready || !backend)
            return rejectedNotReady()
        var nextX = Math.round(Number(x))
        var nextY = Math.round(Number(y))
        return watchAction(backend.moveAvatar(nextX, nextY), function () {
            localMotionX = clampCoordinate(nextX)
            localMotionY = clampCoordinate(nextY)
        })
    }

    function wearProp(propId) {
        if (!ready || !backend)
            return rejectedNotReady()
        var selectedProp = String(propId)
        if (!validAssetIdentifier(selectedProp)) {
            invocationError = "rejected=prop-id-invalid"
            ++invocationSequence
            return invocationError
        }
        return watchAction(backend.wearProp(selectedProp), function () {
            localWornPropId = selectedProp
        })
    }

    function removeProp(propId) {
        if (!ready || !backend)
            return rejectedNotReady()
        var selectedProp = String(propId)
        if (!validAssetIdentifier(selectedProp)) {
            invocationError = "rejected=prop-id-invalid"
            ++invocationSequence
            return invocationError
        }
        return watchAction(backend.removeProp(selectedProp), function () {
            if (localWornPropId === selectedProp)
                localWornPropId = ""
        })
    }

    function refreshPresence() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshPresence(), null)
    }

    function gate3StartStorage(configJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startStorage(String(configJson)), null)
    }

    // Product flow: the user starts a Storage node in Logos Control, then
    // explicitly attaches this Palace session. No Storage configuration is
    // embedded in Palace or inferred from the room setup.
    function connectStorage() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.connectStorage(), null, function (receipt) {
            if (String(receipt).indexOf("storage=stopped") >= 0) {
                invocationError =
                    "Storage is not running. Start it in Logos Control, then connect it here."
            }
        })
    }

    function gate3FetchPng(sourceCid, derivativeCid, byteLength,
                           contentSha256, width, height) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.fetchPngDerivative(
                String(sourceCid),
                String(derivativeCid),
                Math.round(Number(byteLength)),
                String(contentSha256),
                Math.round(Number(width)),
                Math.round(Number(height))),
            null)
    }

    function gate3AssetStatus(derivativeCid) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.assetStatus(String(derivativeCid)), null)
    }

    function gate3PublishPng(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.publishVerifiedPng(String(handle)), null)
    }

    function gate3PublicationStatus(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.publicationStatus(String(handle)), null)
    }

    function assetAuthoringReadOnlyMessage() {
        var authority = encodedStatusValue(
            assetAuthoringCapabilityState, "authority")
        if (authority === "finalized" || authority === "local-committed") {
            return "Read-only. Only Palace owner can approve, upload, or assign assets."
        }
        return "Read-only. Only current draft creator can change assets."
    }

    function roomSetupPublishReadinessValue() {
        if (!canManageAssets)
            return "owner-required"
        if (encodedStatusValue(storageStatus, "storage") !== "running"
                || encodedStatusValue(
                    storageStatus, "callback_registration") !== "ready"
                || encodedStatusValue(
                    storageStatus, "reconciliation_required") !== "0") {
            return "storage-not-ready"
        }

        var state
        try {
            state = JSON.parse(assetAuthoringState)
        } catch (error) {
            return "assignments-unavailable"
        }
        if (!state || !state.roomAssignments
                || !Array.isArray(state.assets)) {
            return "assignments-unavailable"
        }
        if (state.bundleLocked === true)
            return "locked"

        var atriumHandle = String(state.roomAssignments.atrium || "")
        var loungeHandle = String(state.roomAssignments.lounge || "")
        if (!/^[0-9a-f]{64}$/.test(atriumHandle)
                || !/^[0-9a-f]{64}$/.test(loungeHandle)) {
            return "room-backgrounds-needed"
        }

        var handles = [atriumHandle, loungeHandle]
        for (var handleIndex = 0; handleIndex < handles.length;
             ++handleIndex) {
            var published = false
            for (var assetIndex = 0; assetIndex < state.assets.length;
                 ++assetIndex) {
                var asset = state.assets[assetIndex] || ({})
                if (String(asset.handle || "") === handles[handleIndex]
                        && asset.reviewState === "approved"
                        && asset.publicationState === "published"
                        && String(asset.cid || "").length > 0) {
                    published = true
                    break
                }
            }
            if (!published)
                return "room-backgrounds-not-published"
        }
        return "ready"
    }

    function roomSetupPublishMessage() {
        switch (roomSetupPublishReadiness) {
        case "ready":
            return "Both room backgrounds are ready to publish."
        case "locked":
            return "Room setup is publishing. Background assignments are locked."
        case "storage-not-ready":
            return "Start Storage in Logos Control, then choose Connect Storage."
        case "room-backgrounds-needed":
            return "Assign published backgrounds to Atrium and Lounge first."
        case "room-backgrounds-not-published":
            return "Both room backgrounds must be approved and uploaded first."
        case "assignments-unavailable":
            return "Room background assignments are not available yet."
        default:
            return assetAuthoringReadOnlyMessage()
        }
    }

    function rejectAssetAuthoringReadOnly() {
        invocationError =
            "rejected=asset-authoring-read-only;reason="
            + encodedStatusValue(
                assetAuthoringCapabilityState, "reason")
        watchedActionReceipt = ""
        ++invocationSequence
        return invocationError
    }

    function reviewAsset(handle, decision) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.reviewAsset(
                String(handle), String(decision)),
            null)
    }

    function publishAsset(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.publishAsset(String(handle)),
            null)
    }

    function assignRoomBackground(roomId, handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.assignRoomBackground(
                String(roomId), String(handle)),
            null)
    }

    function assignPropAsset(propId, handle, anchorX, anchorY, layer) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.assignPropAsset(
                String(propId),
                String(handle),
                Math.round(Number(anchorX)),
                Math.round(Number(anchorY)),
                String(layer)),
            null)
    }

    function refreshAssetAuthoring() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshAssetAuthoring(), null)
    }

    // Walk the live QML tree for an objectName. Used by e2e to bring
    // moderation controls inside the clipped authoring Flickable before
    // synthetic mouse clicks (row-2 "Set Atrium/Lounge" bottoms otherwise
    // land on the panel chrome and the catalog never updates).
    function findNamedDescendant(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        var children = item.children
        if (!children)
            return null
        for (var index = 0; index < children.length; ++index) {
            var found = findNamedDescendant(children[index], name)
            if (found)
                return found
        }
        return null
    }

    function ensureModerationControlVisible(objectName) {
        var name = String(objectName || "")
        if (name.length === 0 || name.indexOf("palace") !== 0)
            return "invalid"
        var grid = findNamedDescendant(root, "palaceBackgroundGrid")
        var control = findNamedDescendant(root, name)
        if (!grid || !control)
            return "missing"
        var content = grid.contentItem
        if (!content)
            return "no-content"
        var point = control.mapToItem(
            content, control.width / 2, control.height / 2)
        var margin = 16
        var top = point.y - (control.height / 2) - margin
        var bottom = point.y + (control.height / 2) + margin
        var viewTop = grid.contentY
        var viewBottom = grid.contentY + grid.height
        if (top < viewTop) {
            grid.contentY = Math.max(0, top)
        } else if (bottom > viewBottom) {
            grid.contentY = Math.min(
                Math.max(0, grid.contentHeight - grid.height),
                bottom - grid.height)
        }
        return "ok"
    }

    function reviewAndPublishAsset(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        invocationError = ""
        logos.watch(
            backend.reviewAsset(String(handle), "approve"),
            function (reviewReceipt) {
                if (String(reviewReceipt).indexOf("rejected=") === 0) {
                    invocationError = String(reviewReceipt)
                    ++invocationSequence
                    return
                }
                logos.watch(
                    backend.publishAsset(String(handle)),
                    function (publishReceipt) {
                        if (String(publishReceipt)
                                .indexOf("rejected=") === 0) {
                            invocationError = String(publishReceipt)
                            ++invocationSequence
                            return
                        }
                        invocationError = ""
                        ++invocationSequence
                    },
                    function (error) {
                        invocationError =
                            "rejected=ui-remote-call;" + String(error)
                        ++invocationSequence
                    })
            },
            function (error) {
                invocationError =
                    "rejected=ui-remote-call;" + String(error)
                ++invocationSequence
            })
        return "pending"
    }

    function safeAssetStageLabel(value) {
        var candidate = String(value || "")
        // Thirty-two UTF-16 code units fit Core's 128-byte UTF-8 label bound.
        // A display name is never authority, so use a generic local label when
        // a bridge result is longer or contains a control character.
        if (candidate.length === 0 || candidate.length > 32
                || /[\u0000-\u001f\u007f]/.test(candidate))
            return "selected-image.png"
        return candidate
    }

    function assetImportElapsedMs() {
        return Math.max(
            0,
            Math.round(Date.now() - assetImportStartedAtUnixMs))
    }

    function encodedChunkByteLength(base64) {
        var encoded = String(base64)
        if (encoded.length === 0 || encoded.length % 4 !== 0
                || !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(encoded))
            return -1
        var padding = encoded.endsWith("==") ? 2
            : (encoded.endsWith("=") ? 1 : 0)
        return Math.floor(encoded.length * 3 / 4) - padding
    }

    function beginAssetImportTrace(generation, byteLength) {
        assetImportTrace = {
            "schema": "logos.palace.user-file-import",
            "version": 1,
            "generation": generation,
            "byteLength": byteLength,
            "chunkBytes": 32 * 1024,
            "begin": null,
            "appends": [],
            "commit": null,
            "totalBytes": 0
        }
    }

    function recordAssetImportBegin(receipt) {
        if (!assetImportTrace)
            return false
        assetImportTrace.begin = {
            "receipt": String(receipt),
            "elapsedMs": assetImportElapsedMs()
        }
        return true
    }

    function recordAssetImportAppend(sequence, base64, receipt) {
        if (!assetImportTrace)
            return false
        var byteLength = encodedChunkByteLength(base64)
        if (byteLength < 0 || byteLength > assetImportTrace.chunkBytes)
            return false
        var nextBytes = assetImportTrace.totalBytes + byteLength
        if (encodedStatusValue(receipt, "next") !== String(sequence + 1)
                || encodedStatusValue(receipt, "bytes")
                   !== String(nextBytes))
            return false
        assetImportTrace.appends.push({
            "sequence": sequence,
            "byteLength": byteLength,
            "receipt": String(receipt),
            "elapsedMs": assetImportElapsedMs()
        })
        assetImportTrace.totalBytes = nextBytes
        return true
    }

    function completeAssetImportTrace(receipt) {
        if (!assetImportTrace
                || assetImportTrace.totalBytes !== assetImportTrace.byteLength)
            return false
        var handle = encodedStatusValue(receipt, "handle")
        var width = Number(encodedStatusValue(receipt, "width"))
        var height = Number(encodedStatusValue(receipt, "height"))
        var byteLength = Number(encodedStatusValue(receipt, "bytes"))
        if (!/^[0-9a-f]{64}$/.test(handle)
                || !Number.isSafeInteger(width) || width <= 0
                || !Number.isSafeInteger(height) || height <= 0
                || byteLength !== assetImportTrace.byteLength)
            return false
        var completed = {
            "schema": assetImportTrace.schema,
            "version": assetImportTrace.version,
            "generation": assetImportTrace.generation,
            "handle": handle,
            "width": width,
            "height": height,
            "byteLength": assetImportTrace.byteLength,
            "chunkBytes": assetImportTrace.chunkBytes,
            "chunkCount": assetImportTrace.appends.length,
            "begin": assetImportTrace.begin,
            "appends": assetImportTrace.appends,
            "commit": {
                "receipt": String(receipt),
                "elapsedMs": assetImportElapsedMs()
            }
        }
        gate3AssetImportEvidence = JSON.stringify(completed)
        return true
    }

    function releaseSelectedFileHandle(selection) {
        if (!selection || typeof userFiles === "undefined")
            return
        var handle = String(selection.handle || "")
        if (handle.length > 0)
            userFiles.release(handle)
    }

    function releaseAssetImportCapability() {
        if (assetImportCapability.length === 0)
            return true
        var capability = assetImportCapability
        assetImportCapability = ""
        if (typeof userFiles === "undefined")
            return false
        return userFiles.release(capability) === true
    }

    function assetImportMatches(generation, requestId, phase) {
        return assetImportRunning
            && assetImportGeneration === generation
            && assetImportRequestId === String(requestId)
            && (phase.length === 0 || assetImportPhase === phase)
    }

    function cancelAssetImportStage(session) {
        if (session.length === 0 || !backend)
            return
        logos.watch(
            backend.cancelAssetStage(session),
            function () {},
            function () {})
    }

    function resetAssetImport() {
        releaseAssetImportCapability()
        assetImportRunning = false
        assetImportPhase = "idle"
        assetImportRequestId = ""
        assetImportSession = ""
        assetImportLabel = ""
        assetImportExpectedSequence = 0
        assetImportPendingSequence = -1
        assetImportStartedAtUnixMs = 0
        assetImportTrace = null
        ++assetImportGeneration
    }

    function abandonAssetImport() {
        if (!assetImportRunning)
            return
        var session = assetImportSession
        resetAssetImport()
        cancelAssetImportStage(session)
    }

    function failAssetImport(generation, requestId, reason) {
        if (!assetImportMatches(generation, requestId, ""))
            return
        var rejected = String(reason)
        if (rejected.indexOf("rejected=") !== 0)
            rejected = "rejected=asset-import;" + rejected
        var session = assetImportSession
        resetAssetImport()
        cancelAssetImportStage(session)
        invocationError = rejected
        ++invocationSequence
    }

    function validSelectedFile(selection) {
        if (!selection || typeof selection !== "object")
            return false
        var byteLength = Number(selection.byteLength)
        return String(selection.handle || "").length > 0
            && Number.isInteger(byteLength)
            && byteLength >= 0
            && byteLength <= assetImportMaximumBytes
    }

    function beginSelectedAssetStage(generation, requestId, label,
                                     capability) {
        if (!assetImportMatches(generation, requestId, "starting-stage")
                || assetImportCapability !== capability)
            return
        logos.watch(
            backend.beginAssetStage(label),
            function (receipt) {
                var receiptString = String(receipt)
                var session = encodedStatusValue(
                    receiptString, "session")
                if (!assetImportMatches(
                        generation, requestId, "starting-stage")
                        || assetImportCapability !== capability) {
                    // A stage may complete after this view abandoned its
                    // request. Cancel its newly-created Core session rather
                    // than leaving a stale authoring slot behind.
                    if (/^[0-9a-f]{32}$/.test(session))
                        cancelAssetImportStage(session)
                    return
                }
                if (receiptString.indexOf("rejected=") === 0) {
                    failAssetImport(generation, requestId, receiptString)
                    return
                }
                if (!/^[0-9a-f]{32}$/.test(session)) {
                    failAssetImport(
                        generation, requestId, "invalid-session-receipt")
                    return
                }
                if (encodedStatusValue(receiptString, "next") !== "0"
                        || encodedStatusValue(
                            receiptString, "maxChunkBytes")
                           !== String(32 * 1024)
                        || encodedStatusValue(
                            receiptString, "maxTotalBytes")
                           !== String(assetImportMaximumBytes)
                        || !recordAssetImportBegin(receiptString)) {
                    failAssetImport(
                        generation, requestId, "invalid-stage-begin-receipt")
                    return
                }
                assetImportSession = session
                assetImportExpectedSequence = 0
                assetImportPendingSequence = -1
                assetImportPhase = "reading"
                appendSelectedAssetChunk(
                    generation, requestId, session, capability)
            },
            function (error) {
                failAssetImport(
                    generation, requestId, "remote-begin;" + String(error))
            })
    }

    function commitSelectedAsset(generation, requestId, session) {
        if (!assetImportMatches(generation, requestId, "committing")
                || assetImportSession !== session)
            return
        logos.watch(
            backend.commitAssetStage(session),
            function (receipt) {
                if (!assetImportMatches(generation, requestId, "committing")
                        || assetImportSession !== session) {
                    cancelAssetImportStage(session)
                    return
                }
                var receiptString = String(receipt)
                if (receiptString.indexOf("rejected=") === 0) {
                    failAssetImport(generation, requestId, receiptString)
                    return
                }
                if (!completeAssetImportTrace(receiptString)) {
                    failAssetImport(
                        generation, requestId,
                        "invalid-stage-commit-receipt")
                    return
                }
                invocationError = ""
                resetAssetImport()
                ++invocationSequence
            },
            function (error) {
                failAssetImport(
                    generation, requestId, "remote-commit;" + String(error))
            })
    }

    function appendSelectedAssetChunk(generation, requestId, session,
                                      capability) {
        if (!assetImportMatches(generation, requestId, "reading")
                || assetImportSession !== session
                || assetImportCapability !== capability)
            return
        var chunk = userFiles.readNextChunk(capability)
        var sequence = Number(chunk && chunk.sequence)
        if (!chunk || typeof chunk.base64 !== "string"
                || !Number.isInteger(sequence)
                || sequence !== assetImportExpectedSequence
                || typeof chunk.eof !== "boolean") {
            failAssetImport(
                generation, requestId, "selected-file-read;invalid-result")
            return
        }
        var base64 = String(chunk.base64)
        var terminal = chunk.eof
        assetImportPendingSequence = sequence
        assetImportPhase = "appending"
        logos.watch(
            backend.appendAssetStageChunk(
                session, sequence, base64),
            function (receipt) {
                if (!assetImportMatches(generation, requestId, "appending")
                        || assetImportSession !== session
                        || assetImportCapability !== capability
                        || assetImportPendingSequence !== sequence) {
                    cancelAssetImportStage(session)
                    return
                }
                var receiptString = String(receipt)
                if (receiptString.indexOf("rejected=") === 0) {
                    failAssetImport(generation, requestId, receiptString)
                    return
                }
                if (!recordAssetImportAppend(
                        sequence, base64, receiptString)) {
                    failAssetImport(
                        generation, requestId,
                        "invalid-stage-append-receipt")
                    return
                }
                assetImportExpectedSequence = sequence + 1
                assetImportPendingSequence = -1
                if (terminal) {
                    // Core owns the final decoded bytes. Revoke the host
                    // snapshot before the asynchronous commit round trip.
                    if (!releaseAssetImportCapability()) {
                        failAssetImport(
                            generation, requestId,
                            "selected-file-release;invalid-result")
                        return
                    }
                    assetImportPhase = "committing"
                    commitSelectedAsset(generation, requestId, session)
                } else {
                    assetImportPhase = "reading"
                    appendSelectedAssetChunk(
                        generation, requestId, session, capability)
                }
            },
            function (error) {
                failAssetImport(
                    generation, requestId, "remote-append;" + String(error))
            })
    }

    function handleAssetFileSelection(requestId, selection) {
        var generation = assetImportGeneration
        var selectedRequestId = String(requestId || "")
        if (!assetImportMatches(generation, selectedRequestId, "selecting")) {
            releaseSelectedFileHandle(selection)
            return
        }
        if (!selection || String(selection.handle || "").length === 0) {
            invocationError = ""
            resetAssetImport()
            ++invocationSequence
            return
        }
        if (!validSelectedFile(selection)) {
            releaseSelectedFileHandle(selection)
            failAssetImport(
                generation, selectedRequestId,
                "file-selection;invalid-result")
            return
        }

        var capability = String(selection.handle)
        assetImportCapability = capability
        assetImportLabel = safeAssetStageLabel(selection.displayName)
        beginAssetImportTrace(generation, Number(selection.byteLength))
        assetImportExpectedSequence = 0
        assetImportPendingSequence = -1
        assetImportPhase = "starting-stage"
        beginSelectedAssetStage(
            generation, selectedRequestId, assetImportLabel, capability)
    }

    function selectAssetFile() {
        if (!ready || !backend || assetImportRunning)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        if (typeof userFiles === "undefined") {
            invocationError = "rejected=selected-file-bridge-unavailable"
            ++invocationSequence
            return invocationError
        }
        var requestId = String(userFiles.openFile(
            ["PNG images (*.png)"], assetImportMaximumBytes) || "")
        if (requestId.length === 0) {
            invocationError = "rejected=file-selection;request-rejected"
            ++invocationSequence
            return invocationError
        }

        ++assetImportGeneration
        assetImportRunning = true
        assetImportPhase = "selecting"
        assetImportRequestId = requestId
        assetImportCapability = ""
        assetImportSession = ""
        assetImportLabel = ""
        assetImportExpectedSequence = 0
        assetImportPendingSequence = -1
        assetImportStartedAtUnixMs = Date.now()
        assetImportTrace = null
        gate3AssetImportEvidence = ""
        invocationError = ""
        return "pending"
    }

    function gate3PublishBundle() {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(backend.publishMvpStorageBundle(), null)
    }

    function publishRoomSetupBundle(onAccepted, onRejected) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.publishMvpStorageBundle(), onAccepted, onRejected)
    }

    function gate3BundleStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.mvpStorageBundleStatus(), null)
    }

    function gate3FetchBundle(catalogBase64) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.fetchMvpStorageBundle(String(catalogBase64)), null)
    }

    function gate3VerifyRetention() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.verifyMvpStorageRetention(), null)
    }

    function gate3ObjectStatus(objectId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.storageObjectStatus(String(objectId)), null)
    }

    function gate3StorageStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.storageSessionStatus(), null)
    }

    function gate3StoragePeerEndpoint() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.storagePeerEndpoint(), null)
    }

    function gate3ConnectStoragePeer(peerId, addressesJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.connectStoragePeer(
                String(peerId), String(addressesJson)),
            null)
    }

    function gate3MarkStorageMaterialized() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.markStorageMaterialized(), null)
    }

    function gate4StartLez(password) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startLez(String(password)), null)
    }

    function gate4CreateIdentity(displayName) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.createIdentity(String(displayName)), null)
    }

    function gate4OpenPalace(palaceUri) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.openPalace(String(palaceUri)), null)
    }

    function gate4PalaceStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshPalace(), null)
    }

    function gate4RefreshLez() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshLez(), null)
    }

    function gate4RefreshIdentity() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshIdentity(), null)
    }

    function gate4BanUser(subjectUserIdHex) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.banUser(String(subjectUserIdHex)), null)
    }

    function gate4BanProp(propId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.banProp(String(propId)), null)
    }

    function delegateModerator(subjectUserIdHex) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.delegateModerator(String(subjectUserIdHex)), null)
    }

    function setRoomLocked(roomId, locked) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.setRoomLocked(String(roomId), Boolean(locked)), null)
    }

    function gate4RefreshModeration() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshModeration(), null)
    }

    function gate4Submit(actionId, stateAccountIdHex,
                         callerAccountIdHex, programIdHex,
                         transitionJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.submitPalaceTransition(
                String(actionId),
                String(stateAccountIdHex),
                String(callerAccountIdHex),
                String(programIdHex),
                String(transitionJson)),
            null)
    }

    function gate4Observe(actionId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.observePalaceTransition(String(actionId)), null)
    }

    function gate4Reconcile(actionId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.reconcilePalaceTransition(String(actionId)), null)
    }

    function observeCreatorAction(actionId, onAccepted, onRejected) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.observePalaceTransition(String(actionId)),
            onAccepted, onRejected)
    }

    function reconcileCreatorAction(actionId, onAccepted, onRejected) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.reconcilePalaceTransition(String(actionId)),
            onAccepted, onRejected)
    }

    function gate4ActionStatus(actionId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.actionStatus(String(actionId)), null)
    }

    function gate5UseDoor() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.useSpot("door"), null)
    }

    function gate5PreviewDoor() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.previewSpot("door"), null)
    }

    function gate5ActionStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshSpot(), null)
    }

    function gate5Reconcile() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.reconcileSpot(), null)
    }

    function gate5VmTurnMetrics(actionId, phase) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.vmTurnMetrics(
                String(actionId), String(phase)),
            null)
    }

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "logos_palace_ui")
                root.ready = isReady && root.backend !== null
        }
    }

    Connections {
        target: typeof userFiles === "undefined" ? null : userFiles
        function onFileSelectionCompleted(requestId, selection) {
            root.handleAssetFileSelection(requestId, selection)
        }
    }

    Component.onCompleted: {
        root.ready = root.backend !== null
            && logos.isViewModuleReady("logos_palace_ui")
        root.focusChatWhenUnobstructed()
    }

    FrameAnimation {
        id: gateFrameTimingAnimation
        running: root.gateFrameTimingRunning
        onTriggered: {
            if (root.gateFrameTimingWarmupsRemaining > 0) {
                --root.gateFrameTimingWarmupsRemaining
                if (root.gateFrameTimingWarmupsRemaining === 0) {
                    root.gateFrameTimingStartFrame = currentFrame
                    root.gateFrameTimingStartElapsedUs =
                        Math.round(elapsedTime * 1000000)
                }
                return
            }
            var sampleUs = Math.round(frameTime * 1000000)
            if (!isFinite(sampleUs)
                    || sampleUs <= 0
                    || sampleUs > 30000000) {
                root.gateFrameTimingFailure = "invalid-frame-interval"
                root.gateFrameTimingRunning = false
                return
            }
            var nextCount = root.gateFrameTimingSamplesUs.length + 1
            if (currentFrame
                    !== root.gateFrameTimingStartFrame + nextCount) {
                root.gateFrameTimingFailure = "invalid-frame-window"
                root.gateFrameTimingRunning = false
                return
            }
            var samples =
                root.gateFrameTimingSamplesUs.concat([sampleUs])
            if (nextCount === root.gateFrameTimingSampleTarget) {
                var elapsedUs = Math.round(elapsedTime * 1000000)
                    - root.gateFrameTimingStartElapsedUs
                var summedUs = 0
                for (var index = 0; index < samples.length; ++index)
                    summedUs += samples[index]
                if (elapsedUs <= 0
                        || elapsedUs > 30000000
                        || Math.abs(summedUs - elapsedUs)
                           > Math.ceil(
                               root.gateFrameTimingSampleTarget / 2)) {
                    root.gateFrameTimingFailure = "invalid-frame-window"
                    root.gateFrameTimingRunning = false
                    return
                }
                root.gateFrameTimingEndFrame = currentFrame
                root.gateFrameTimingElapsedTimeUs = elapsedUs
            }
            root.gateFrameTimingSamplesUs = samples
            if (nextCount === root.gateFrameTimingSampleTarget)
                root.gateFrameTimingRunning = false
        }
    }

    Timer {
        interval: 30000
        repeat: false
        running: root.gateFrameTimingRunning
        onTriggered: {
            root.gateFrameTimingFailure = "capture-timeout"
            root.gateFrameTimingRunning = false
        }
    }

    Timer {
        id: onboardingBundlePollTimer
        interval: 500
        repeat: true
        running: root.onboardingBundlePolling
            && root.ready && root.backend !== null
        onTriggered: root.pollRoomSetupPublication()
    }

    Timer {
        id: onboardingFinalityPollTimer
        interval: 500
        repeat: true
        running: root.onboardingFinalityPolling
            && root.ready && root.backend !== null
        onTriggered: root.pollCreatorAction()
    }

    Timer {
        id: onboardingJoinRegistrationPollTimer
        interval: 500
        repeat: true
        running: root.onboardingJoinRegistrationPolling
            && root.ready && root.backend !== null
        onTriggered: root.pollExistingPalaceRegistration()
    }

    Timer {
        id: onboardingCreatedPalaceRetryTimer
        interval: 500
        repeat: false
        onTriggered: {
            if (root.onboardingPhase === "creating-palace"
                    && root.ready && root.backend !== null) {
                root.createOnboardingPalace()
            } else if (root.onboardingPhase === "waiting-created-palace"
                    && root.ready && root.backend !== null
                    && !root.palaceOpen) {
                root.openCreatedPalace()
            } else if (root.onboardingPhase
                    === "waiting-initial-room-state"
                    && root.ready && root.backend !== null) {
                root.prepareInitialRoomState()
            }
        }
    }

    Component.onDestruction: root.abandonAssetImport()

    onBackgroundModerationOpenChanged: {
        if (!backgroundModerationOpen) {
            resetAuthoringPreviewState()
            focusChatWhenUnobstructed()
        }
    }

    onPropBagOpenChanged: {
        if (!propBagOpen)
            focusChatWhenUnobstructed()
    }

    onRoomListOpenChanged: {
        if (!roomListOpen)
            focusChatWhenUnobstructed()
    }

    onUserListOpenChanged: {
        if (!userListOpen)
            focusChatWhenUnobstructed()
    }

    onReadyChanged: {
        if (ready)
            focusChatWhenUnobstructed()
    }

    onPalaceStateChanged: {
        var state = encodedStatusValue(palaceState, "palace")
        if (palaceOpen) {
            if (onboardingPhase === "opening-palace"
                    || onboardingPhase === "waiting-palace"
                    || onboardingPhase === "opening-created-palace"
                    || onboardingPhase === "waiting-created-palace") {
                if (onboardingUsesExistingPalace)
                    beginExistingPalaceRegistration()
                else {
                    onboardingPhase = "complete"
                    onboardingError = ""
                    clearOnboardingPassword()
                }
            } else if (onboardingPhase
                    === "waiting-initial-room-state") {
                createInitialRoomState()
            }
        } else if (state === "rejected" || state === "degraded") {
            if (onboardingPhase === "waiting-palace")
                onboardingFail("palace", palaceState)
            else if (onboardingPhase === "waiting-created-palace"
                    && retryableCreatedPalaceOpenState(palaceState)) {
                if (!onboardingCreatedPalaceRetryTimer.running)
                    onboardingCreatedPalaceRetryTimer.start()
            } else if (onboardingPhase === "waiting-created-palace")
                onboardingFail("created-palace", palaceState)
            else if (onboardingPhase
                    === "waiting-initial-room-state")
                onboardingFail("finality", palaceState)
        }
    }

    onRoomUsableChanged: {
        if (!roomUsable) {
            backgroundModerationOpen = false
            propBagOpen = false
            roomListOpen = false
            userListOpen = false
            return
        }
        focusChatWhenUnobstructed()
    }

    onParticipantsChanged: syncSelectedModerationUser()

    Shortcut {
        sequence: "Esc"
        enabled: root.roomUsable
            && (root.backgroundModerationOpen || root.propBagOpen
            || root.roomListOpen || root.userListOpen
                )
        onActivated: root.closeActiveUtilityPanel()
    }

    FrameAnimation {
        running: root.backgroundScreenshotFenceRunning
        onTriggered: {
            root.backgroundScreenshotFenceFrame = currentFrame
            root.backgroundScreenshotFenceRunning = false
            ++root.backgroundPreviewEpoch
        }
    }

    // Palace Chat Official / classic main window:
    // top utility toolbar → View Screen → Users strip → input + bag/trash.
    Rectangle {
        anchors.fill: parent
        enabled: root.roomUsable
        color: "#c0c0c0"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 0

            // Horizontal toolbar (PalaceChat Official arrangement).
            Rectangle {
                id: toolbox
                objectName: "palaceToolbox"
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                color: "#d4d0c8"
                border.color: "#808080"

                Row {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 4
                    spacing: 3

                    Button {
                        objectName: "palaceToolboxDoor"
                        width: 32
                        height: 28
                        text: "🚪"
                        font.pixelSize: 12
                        Accessible.name: "Door / room exit"
                        ToolTip.visible: hovered
                        ToolTip.text: "Door / room exit"
                        enabled: root.ready
                            && (root.roomTitle !== "Atrium"
                                || !root.gate5DoorBlocked)
                        onClicked: root.selectFixedRoom(
                            root.roomTitle === "Atrium" ? "lounge" : "atrium")
                    }
                    Button {
                        objectName: "palaceToolboxRooms"
                        width: 32
                        height: 28
                        text: "⌂"
                        font.pixelSize: 14
                        Accessible.name: "Rooms"
                        ToolTip.visible: hovered
                        ToolTip.text: "Rooms"
                        enabled: root.ready
                        onClicked: root.roomListOpen = !root.roomListOpen
                    }
                    Button {
                        objectName: "palaceMoveUp"
                        width: 28
                        height: 28
                        text: "↑"
                        Accessible.name: "Move up"
                        enabled: root.ready
                        onClicked: root.moveAvatar(
                            root.localMotionX, root.localMotionY - 750)
                    }
                    Button {
                        objectName: "palaceMoveLeft"
                        width: 28
                        height: 28
                        text: "←"
                        Accessible.name: "Move left"
                        enabled: root.ready
                        onClicked: root.moveAvatar(
                            root.localMotionX - 750, root.localMotionY)
                    }
                    Button {
                        objectName: "palaceMoveRight"
                        width: 28
                        height: 28
                        text: "→"
                        Accessible.name: "Move right"
                        enabled: root.ready
                        onClicked: root.moveAvatar(
                            root.localMotionX + 750, root.localMotionY)
                    }
                    Button {
                        objectName: "palaceMoveDown"
                        width: 28
                        height: 28
                        text: "↓"
                        Accessible.name: "Move down"
                        enabled: root.ready
                        onClicked: root.moveAvatar(
                            root.localMotionX, root.localMotionY + 750)
                    }
                    Rectangle {
                        width: 1
                        height: 22
                        color: "#808080"
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Button {
                        objectName: "palaceWearAssignedProp"
                        width: 32
                        height: 28
                        text: "☺+"
                        font.pixelSize: 11
                        Accessible.name: "Wear assigned prop"
                        ToolTip.visible: hovered
                        ToolTip.text: "Wear assigned prop"
                        enabled: root.ready
                            && root.availablePropId.length > 0
                            && root.localWornPropId
                                !== root.availablePropId
                        onClicked: root.wearProp(root.availablePropId)
                    }
                    Button {
                        objectName: "palaceRemoveAssignedProp"
                        width: 32
                        height: 28
                        text: "☺−"
                        font.pixelSize: 11
                        Accessible.name: "Remove worn prop"
                        ToolTip.visible: hovered
                        ToolTip.text: "Remove worn prop"
                        enabled: root.ready
                            && root.availablePropId.length > 0
                            && root.localWornPropId
                                === root.availablePropId
                        onClicked: root.removeProp(root.availablePropId)
                    }
                    Button {
                        objectName: "palaceBackgroundModerationButton"
                        width: 56
                        height: 28
                        text: "Assets"
                        font.pixelSize: 10
                        Accessible.name: "Assets"
                        enabled: root.ready
                        ToolTip.visible: hovered && !root.canManageAssets
                        ToolTip.text: root.assetAuthoringReadOnlyMessage()
                        onClicked: root.backgroundModerationOpen = true
                    }
                    Button {
                        objectName: "palaceUserListToggle"
                        width: 56
                        height: 28
                        text: "Users"
                        font.pixelSize: 10
                        Accessible.name: "User list"
                        enabled: root.ready
                        onClicked: root.userListOpen = !root.userListOpen
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 7

                    Rectangle {
                        objectName: "palaceLocalDevelopmentIndicator"
                        visible: root.localDevelopmentMode
                        width: localDevelopmentIndicatorText.implicitWidth + 12
                        height: 20
                        radius: 2
                        color: "#e7edf3"
                        border.color: "#8197ad"

                        Text {
                            id: localDevelopmentIndicatorText
                            anchors.centerIn: parent
                            text: "Local development · public finality unavailable"
                            color: "#24394d"
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: root.roomTitle
                        color: "#000080"
                        font.bold: true
                        font.pixelSize: 12
                    }
                }
            }

            Rectangle {
                id: roomCanvas
                objectName: "palaceRoomCanvas"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "#312a24"
                border.color: "#404040"
                border.width: 1
                clip: true

                    Image {
                        id: roomBackground
                        objectName: "palaceRoomBackground"
                        anchors.fill: parent
                        source: root.roomBackgroundHandle.length === 64
                            ? "image://basecamp-verified/"
                              + root.roomBackgroundHandle
                            : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: false
                    }

                    Rectangle {
                        objectName: "palaceRoomBackgroundPlaceholder"
                        anchors.fill: parent
                        // Classic empty room floor — no large diagnostic overlay
                        // (diagnostics stay in the compact delivery strip).
                        color: "#3a342c"
                        visible: roomBackground.status !== Image.Ready
                    }

                    Rectangle {
                        anchors.fill: parent
                        color: "#1b130d"
                        opacity: roomBackground.status === Image.Ready
                            ? 0.12 : 0
                    }

            // Canvas motion sits above the room art but below people and door.
            MouseArea {
                id: roomMoveSurface
                objectName: "palaceRoomMoveSurface"
                anchors.fill: parent
                z: 2
                enabled: root.ready
                acceptedButtons: Qt.LeftButton
                onClicked: function(mouse) {
                    var coordinate = root.canvasPixelsToProtocol(
                        mouse.x, mouse.y)
                    root.moveAvatar(coordinate.x, coordinate.y)
                }
            }

            Item {
                id: participantLayer
                objectName: "palaceParticipants"
                anchors.fill: parent
                z: 4

                Repeater {
                    model: root.participants.length

                    delegate: Item {
                        id: participantDelegate
                        objectName: "palaceParticipant"
                        width: 94
                        height: 116

                        required property int index
                        property var participant:
                            root.participants[index] || ({})
                        property string participantUserId:
                            String(participant.userId || "")
                        property string participantDisplayName:
                            String(participant.displayName || participantUserId)
                        property real participantMotionX:
                            participant.x === null || participant.x === undefined
                                ? -1 : Number(participant.x)
                        property real participantMotionY:
                            participant.y === null || participant.y === undefined
                                ? -1 : Number(participant.y)
                        property real participantX: participantMotionX
                        property real participantY: participantMotionY
                        property real layoutMotionX: root.protocolCoordinate(
                            participant.x, index, root.participants.length)
                        property real layoutMotionY: root.protocolCoordinate(
                            participant.y, root.participants.length - index - 1,
                            root.participants.length)
                        property string participantSpeech:
                            String(participant.speech || "")
                        property var participantPropList:
                            Array.isArray(participant.props)
                                ? participant.props : []
                        property string participantProps:
                            JSON.stringify(participantPropList)

                        x: root.roomX(layoutMotionX) - width / 2
                        y: root.roomY(layoutMotionY) - height / 2

                        Behavior on x {
                            NumberAnimation {
                                duration: 180
                                easing.type: Easing.OutQuad
                            }
                        }
                        Behavior on y {
                            NumberAnimation {
                                duration: 180
                                easing.type: Easing.OutQuad
                            }
                        }

                        Rectangle {
                            id: remoteSpeechCard
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: remoteAvatar.top
                            anchors.bottomMargin: 9
                            width: Math.min(184, Math.max(
                                88, remoteSpeechText.implicitWidth + 24))
                            height: Math.min(
                                root.roomCanvasSpeechMaximumHeight,
                                remoteSpeechText.implicitHeight + 16)
                            radius: 12
                            color: "#fff8e7"
                            border.color: "#8c7145"
                            visible: participantDelegate.participantSpeech.length > 0

                            Text {
                                id: remoteSpeechText
                                objectName: "palaceSpeechBubble"
                                property string participantUserId:
                                    participantDelegate.participantUserId
                                anchors.centerIn: parent
                                width: Math.min(158, implicitWidth)
                                height: Math.min(
                                    root.roomCanvasSpeechMaximumHeight - 16,
                                    implicitHeight)
                                text: participantDelegate.participantSpeech
                                color: "#2b2016"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                                clip: true
                            }
                        }

                        // Classic "roundhead" presence disc (Palace default face).
                        Rectangle {
                            id: remoteAvatar
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            width: 54
                            height: 54
                            radius: width / 2
                            color: "#ffe566"
                            border.color: "#222222"
                            border.width: 2

                            // Eyes
                            Row {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.topMargin: 14
                                spacing: 10
                                Rectangle {
                                    width: 7
                                    height: 9
                                    radius: 3
                                    color: "#111111"
                                }
                                Rectangle {
                                    width: 7
                                    height: 9
                                    radius: 3
                                    color: "#111111"
                                }
                            }
                            // Smile
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 12
                                width: 18
                                height: 9
                                radius: 9
                                color: "transparent"
                                border.color: "#111111"
                                border.width: 2
                                // lower half only: clip via parent disc
                            }
                            Text {
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: 6
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "ᴗ"
                                color: "#111111"
                                font.pixelSize: 16
                                font.bold: true
                            }

                            Rectangle {
                                anchors.centerIn: parent
                                width: parent.width + 10
                                height: parent.height + 10
                                radius: width / 2
                                color: "transparent"
                                border.width: 2
                                border.color:
                                    root.selectedModerationUserId
                                    === participantDelegate.participantUserId
                                    ? "#2b6aa4" : "transparent"
                            }
                        }

                        MouseArea {
                            objectName: "palaceParticipantSelect"
                            anchors.fill: remoteAvatar
                            enabled: participantDelegate.participantUserId
                                .length > 0
                            cursorShape: enabled
                                ? Qt.PointingHandCursor
                                : Qt.ArrowCursor
                            acceptedButtons: Qt.LeftButton
                            onClicked:
                                root.selectedModerationUserId
                                = participantDelegate.participantUserId
                        }

                        Item {
                            id: wornProp
                            objectName: "palaceWornProp"
                            property string participantUserId:
                                participantDelegate.participantUserId
                            property string propId:
                                root.activePropAsset.available === true
                                ? String(root.activePropAsset.propId)
                                : (participantDelegate.participantPropList
                                   .length > 0
                                   ? String(participantDelegate
                                            .participantPropList[0])
                                   : "")
                            property string assetHandle:
                                root.activePropAsset.available === true
                                ? String(root.activePropAsset.handle) : ""
                            property bool assetAvailable:
                                root.activePropAsset.available === true
                                && assetHandle.length === 64
                            property real sourceWidth: assetAvailable
                                ? Number(root.activePropAsset.width) : 42
                            property real sourceHeight: assetAvailable
                                ? Number(root.activePropAsset.height) : 18
                            property real renderScale: assetAvailable
                                ? Math.min(
                                    1,
                                    72 / Math.max(1, sourceWidth),
                                    72 / Math.max(1, sourceHeight))
                                : 1
                            property real targetX:
                                String(root.activePropAsset.layer) === "hand"
                                ? remoteAvatar.x + remoteAvatar.width
                                : (String(root.activePropAsset.layer) === "back"
                                   ? remoteAvatar.x
                                   : remoteAvatar.x
                                     + remoteAvatar.width / 2)
                            property real targetY:
                                String(root.activePropAsset.layer) === "head"
                                ? remoteAvatar.y + 8
                                : (String(root.activePropAsset.layer) === "body"
                                   ? remoteAvatar.y
                                     + remoteAvatar.height / 2
                                   : remoteAvatar.y
                                     + remoteAvatar.height * 0.62)
                            property string renderState:
                                assetAvailable ? "verified-image"
                                               : "asset-pending"
                            x: assetAvailable
                                ? targetX
                                  - Number(root.activePropAsset.anchorX)
                                    * renderScale
                                : remoteAvatar.x
                                  + (remoteAvatar.width - width) / 2
                            y: assetAvailable
                                ? targetY
                                  - Number(root.activePropAsset.anchorY)
                                    * renderScale
                                : remoteAvatar.y - height + 8
                            width: sourceWidth * renderScale
                            height: sourceHeight * renderScale
                            visible: participantDelegate.participantPropList
                                .indexOf(propId) !== -1
                            z: assetAvailable
                               && String(root.activePropAsset.layer) === "back"
                                ? -1 : 1

                            Image {
                                anchors.fill: parent
                                visible: wornProp.assetAvailable
                                source: visible
                                    ? "image://basecamp-verified/"
                                      + wornProp.assetHandle
                                    : ""
                                fillMode: Image.Stretch
                                asynchronous: false
                                smooth: true
                            }

                            Rectangle {
                                anchors.fill: parent
                                visible: !wornProp.assetAvailable
                                radius: 4
                                color: "#3b342cdd"
                                border.color: "#f3c36b"

                                Text {
                                    anchors.centerIn: parent
                                    text: "asset pending"
                                    color: "#fff2cf"
                                    font.pixelSize: 7
                                }
                            }
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: remoteAvatar.bottom
                            anchors.topMargin: 6
                            width: Math.min(120, participantName.implicitWidth + 18)
                            height: 24
                            radius: 12
                            color: "#211a14dd"

                            Text {
                                id: participantName
                                anchors.centerIn: parent
                                text: participantDelegate.participantDisplayName
                                color: "#fff2cf"
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                    }
                }
            }

            // In-scene door affordance (diegetic navigation).
            Button {
                id: roomDoor
                objectName: "palaceRoomDoor"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: root.roomCanvasDoorBottomMargin
                width: 154
                height: root.roomCanvasDoorHeight
                text: root.roomTitle !== "Atrium"
                    ? "Door to Atrium"
                    : (root.gate5DoorBlocked
                       ? "Door finalizing…" : "Door to Lounge")
                font.bold: true
                font.pixelSize: 12
                Accessible.name: text
                enabled: root.ready
                    && (root.roomTitle !== "Atrium"
                        || !root.gate5DoorBlocked)
                z: 10
                ToolTip.visible: hovered
                ToolTip.text: text

                background: Rectangle {
                    radius: 5
                    color: roomDoor.down ? "#74421f" : "#9a6030"
                    border.color: "#f3cf8a"
                    border.width: 2

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: 28
                        height: parent.height - 12
                        radius: 2
                        color: "#4d2918"
                        border.color: "#e3b76a"
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 29
                        anchors.verticalCenter: parent.verticalCenter
                        width: 5
                        height: 5
                        radius: 3
                        color: "#f9db79"
                    }
                }

                contentItem: Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 44
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: roomDoor.text
                    color: "#fff8e7"
                    font: roomDoor.font
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.selectFixedRoom(
                    root.roomTitle === "Atrium" ? "lounge" : "atrium")
            }
                } // roomCanvas

            // Occupancy strip (PalaceChat: room name | Users:n/max | bag | trash).
            Rectangle {
                id: statusStrip
                objectName: "palaceStatusStrip"
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                color: "#d4d0c8"
                border.color: "#808080"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 4
                    spacing: 6

                    Text {
                        text: root.roomTitle
                        color: "#000000"
                        font.pixelSize: 11
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: "Users: " + root.participants.length
                            + "/" + Math.max(
                                32, root.participants.length)
                        color: "#000000"
                        font.pixelSize: 11
                    }
                    Button {
                        objectName: "palaceStatusSelectedUser"
                        text: root.selectedModerationUserName().length > 0
                            ? "Op: "
                              + root.selectedModerationUserName()
                            : "Op: select user"
                        Accessible.name: text
                        Layout.preferredHeight: 22
                        Layout.preferredWidth: 136
                        enabled: root.ready
                        onClicked: root.userListOpen = true
                    }
                    Button {
                        objectName: "palaceStatusBanUserButton"
                        property string subjectUserId:
                            root.selectedModerationUserId
                        text: "Ban"
                        Accessible.name: "Ban selected user"
                        Layout.preferredHeight: 22
                        Layout.preferredWidth: 42
                        visible: root.canBanUser
                        enabled: root.canBanUser
                            && subjectUserId.length === 64
                            && root.selectedModerationUser() !== null
                        onClicked: root.gate4BanUser(subjectUserId)
                    }
                    // Classic suitcase / trash sit on the status strip.
                    Button {
                        objectName: "palacePropBag"
                        text: "🧳"
                        Accessible.name: "Prop bag"
                        Layout.preferredHeight: 22
                        Layout.preferredWidth: 28
                        ToolTip.visible: hovered
                        ToolTip.text: "Prop bag"
                        enabled: root.ready
                        onClicked: root.propBagOpen = !root.propBagOpen
                    }
                    Button {
                        objectName: "palacePropTrash"
                        text: "🗑"
                        Accessible.name: root.availablePropId.length > 0
                            ? "Discard or ban assigned prop" : "Trash"
                        Layout.preferredHeight: 22
                        Layout.preferredWidth: 28
                        ToolTip.visible: hovered
                        ToolTip.text: root.availablePropId.length > 0
                            ? "Discard / ban assigned prop"
                            : "Trash"
                        visible: root.canBanProp
                        enabled: root.canBanProp
                            && root.availablePropId.length > 0
                        onClicked: {
                            if (root.localWornPropId
                                    === root.availablePropId)
                                root.removeProp(root.availablePropId)
                            root.gate4BanProp(root.availablePropId)
                        }
                    }
                }
            }

            // Single-line chat under the room (classic Speak field).
            Rectangle {
                id: inputStrip
                objectName: "palaceInputStrip"
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                color: "#d4d0c8"
                border.color: "#808080"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 4

                    TextField {
                        id: chatInput
                        objectName: "palaceChatInput"
                        Layout.fillWidth: true
                        Layout.preferredHeight: 24
                        placeholderText: ""
                        maximumLength: 280
                        Accessible.name: "Chat message"
                        enabled: root.ready
                        onAccepted: {
                            if (text.length > 0) {
                                root.sendSpeech(text)
                                clear()
                            }
                        }
                    }
                    Button {
                        objectName: "palaceSayButton"
                        text: "Say"
                        Accessible.name: "Send chat message"
                        Layout.preferredHeight: 24
                        Layout.preferredWidth: 44
                        enabled: root.ready && chatInput.text.length > 0
                        onClicked: {
                            root.sendSpeech(chatInput.text)
                            chatInput.clear()
                        }
                    }
                }
            }

            Text {
                objectName: "palaceLastActionReceipt"
                Layout.fillWidth: true
                Layout.preferredHeight: 14
                text: root.lastActionReceipt.length > 0
                    ? root.lastActionReceipt : ""
                color: root.lastActionReceipt.indexOf("rejected=") === 0
                       || root.lastActionReceipt.indexOf(
                           "degraded;reason=") === 0
                    ? "#800000" : "#006400"
                font.pixelSize: 9
                elide: Text.ElideRight
                visible: root.lastActionReceipt.length > 0
            }
        } // main ColumnLayout

        // Fixed, visible room palette: no inferred routing or hidden rooms.
        Rectangle {
            id: roomListPanel
            objectName: "palaceRoomListPanel"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 8
            anchors.topMargin: 42
            width: 164
            height: 92
            radius: 2
            color: "#f5f5f5"
            border.color: "#404040"
            z: 20
            visible: root.roomListOpen

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 3

                Text {
                    text: "Rooms"
                    color: "#000000"
                    font.bold: true
                    font.pixelSize: 11
                }

                Button {
                    objectName: "palaceRoomListAtrium"
                    width: 150
                    height: 25
                    text: root.roomTitle === "Atrium"
                        ? "Atrium (current)" : "Atrium"
                    enabled: root.ready
                    onClicked: {
                        root.selectFixedRoom("atrium")
                        root.roomListOpen = false
                    }
                }

                Button {
                    objectName: "palaceRoomListLounge"
                    width: 150
                    height: 25
                    text: root.roomTitle === "Lounge"
                        ? "Lounge (current)" : "Lounge"
                    enabled: root.ready
                        && (root.roomTitle !== "Atrium"
                            || !root.gate5DoorBlocked)
                    onClicked: {
                        root.selectFixedRoom("lounge")
                        root.roomListOpen = false
                    }
                }
            }
        }

        // Prop bag panel (compositional identity: wear/drop assigned prop).
        Rectangle {
            objectName: "palacePropBagPanel"
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 8
            anchors.bottomMargin: 48
            width: 200
            height: 132
            radius: 4
            color: "#f0f0f0"
            border.color: "#404040"
            z: 25
            visible: root.propBagOpen

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                Text {
                    text: "Prop bag"
                    font.bold: true
                    font.pixelSize: 12
                    color: "#000000"
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: 10
                    color: "#222222"
                    text: root.availablePropId.length > 0
                        ? ("Assigned prop: " + root.availablePropId
                           + (root.localWornPropId
                              === root.availablePropId
                              ? " (worn)" : " (in bag)"))
                        : "No assigned prop yet. Operator can set one in Assets."
                }
                Row {
                    spacing: 6
                    Button {
                        text: "Wear"
                        width: 72
                        height: 26
                        enabled: root.ready
                            && root.availablePropId.length > 0
                            && root.localWornPropId
                                !== root.availablePropId
                        onClicked: root.wearProp(root.availablePropId)
                    }
                    Button {
                        text: "Remove"
                        width: 72
                        height: 26
                        enabled: root.ready
                            && root.availablePropId.length > 0
                            && root.localWornPropId
                                === root.availablePropId
                        onClicked: root.removeProp(root.availablePropId)
                    }
                }
                Button {
                    text: "Close bag"
                    width: 100
                    height: 24
                    onClicked: root.propBagOpen = false
                }
            }
        }

        // Operator User List (classic compact list with Ban ≈ Kill).
        Rectangle {
            objectName: "palaceModerationPanel"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 8
            anchors.topMargin: 42
            width: 176
            height: Math.min(
                380, 300
                + (root.availablePropId.length > 0 ? 28 : 0)
                + Math.min(root.participants.length, 32) * 2)
            radius: 2
            color: "#f5f5f5"
            border.color: "#404040"
            z: 20
            visible: root.userListOpen

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 3

                Text {
                    text: "User List"
                    color: "#000000"
                    font.bold: true
                    font.pixelSize: 11
                }

                Flickable {
                    objectName: "palaceModerationRoster"
                    width: 164
                    height: Math.max(
                        24,
                        parent.height
                        - 100
                        - (root.availablePropId.length > 0 ? 28 : 0))
                    contentWidth: width
                    contentHeight: moderationRosterContent.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick

                    Column {
                        id: moderationRosterContent
                        width: parent.width
                        spacing: 3

                        Repeater {
                            model: root.participants.length

                            delegate: Button {
                                id: rosterUser
                                objectName: "palaceModerationRosterUser"
                                required property int index
                                property var participant:
                                    root.participants[index] || ({})
                                property string participantName:
                                    String(participant.displayName
                                           || participant.userId || "Unknown")
                                property string participantUserId:
                                    String(participant.userId || "")
                                width: 164
                                height: 24
                                text: participantName
                                font.pixelSize: 10
                                checkable: true
                                checked: root.selectedModerationUserId
                                    === participantUserId
                                enabled: participantUserId.length > 0
                                Accessible.name: "Select " + participantName
                                ToolTip.visible: hovered
                                ToolTip.text: "Select " + participantName
                                onClicked: root.selectedModerationUserId
                                    = participantUserId

                                contentItem: Text {
                                    anchors.left: parent.left
                                    anchors.leftMargin: 6
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: rosterUser.text
                                    elide: Text.ElideRight
                                    color: "#000000"
                                    font: rosterUser.font
                                    verticalAlignment: Text.AlignVCenter
                                }

                                background: Rectangle {
                                    color: rosterUser.checked
                                        ? "#b6d8ff" : "transparent"
                                    border.color: rosterUser.checked
                                        ? "#34699a" : "transparent"
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    objectName: "palaceModerationUserFooter"
                    width: 164
                    height: 46
                    color: "#e8e8e8"
                    border.color: "#9a9a9a"

                    Column {
                        anchors.fill: parent
                        anchors.margins: 3
                        spacing: 2

                        Text {
                            width: parent.width
                            text: root.selectedModerationUserName().length > 0
                                ? "Selected: "
                                  + root.selectedModerationUserName()
                                : "Select a user"
                            color: "#333333"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                        }

                        Button {
                            objectName: "palaceBanUserButton"
                            property string subjectUserId:
                                root.selectedModerationUserId
                            width: parent.width
                            height: 22
                            text: root.selectedModerationUserName().length > 0
                                ? "Ban " + root.selectedModerationUserName()
                                : "Ban selected user"
                            font.pixelSize: 9
                            visible: root.canBanUser
                            enabled: root.canBanUser
                                && subjectUserId.length === 64
                                && root.selectedModerationUser() !== null
                            Accessible.name: text
                            ToolTip.visible: hovered
                            ToolTip.text: "Ban selected user"
                            onClicked: root.gate4BanUser(subjectUserId)
                        }

                        Button {
                            objectName: "palaceDelegateModeratorButton"
                            property string subjectUserId:
                                root.selectedModerationUserId
                            width: parent.width
                            height: 22
                            text: "Make moderator"
                            font.pixelSize: 9
                            visible: root.canDelegateModerator
                            enabled: root.canDelegateModerator
                                && subjectUserId.length === 64
                                && root.selectedModerationUser() !== null
                            Accessible.name: text
                            ToolTip.visible: hovered
                            ToolTip.text: "Grant moderation and room-lock capability"
                            onClicked: root.delegateModerator(subjectUserId)
                        }
                    }
                }

                Button {
                    objectName: "palaceBanAssignedPropButton"
                    width: 148
                    height: 24
                    text: "Ban assigned prop"
                    font.pixelSize: 9
                    visible: root.canBanProp
                        && root.availablePropId.length > 0
                    enabled: root.canBanProp
                    onClicked: root.gate4BanProp(
                        root.availablePropId)
                }

                Button {
                    objectName: "palaceRoomLockButton"
                    width: 148
                    height: 24
                    text: root.roomLocked ? "Unlock " + root.roomTitle
                                           : "Lock " + root.roomTitle
                    font.pixelSize: 9
                    visible: root.canSetRoomLock
                    enabled: root.canSetRoomLock
                    onClicked: root.setRoomLocked(
                        root.roomTitle, !root.roomLocked)
                }

                Text {
                    objectName: "palaceModerationStatus"
                    width: 164
                    text: "Moderation: "
                        + (root.encodedStatusValue(
                            root.moderationState, "state") || "idle")
                    color: "#333333"
                    elide: Text.ElideRight
                    font.pixelSize: 9
                }
            }
        }

    }

        // Authoring modal: Loader unloads cards when closed (preview leak fix).
        Loader {
            id: backgroundModerationLoader
            anchors.centerIn: parent
            width: Math.min(parent.width - 48, 930)
            height: Math.min(parent.height - 48, 620)
            z: 50
            active: root.backgroundModerationOpen
            sourceComponent: backgroundModerationComponent
            onActiveChanged: {
                if (!active)
                    root.resetAuthoringPreviewState()
            }
        }

        Component {
            id: backgroundModerationComponent

            Rectangle {
            id: backgroundModeration
            objectName: "palaceBackgroundModeration"
            width: backgroundModerationLoader.width
            height: backgroundModerationLoader.height
            radius: 8
            color: "#f518130f"
            border.color: "#d5b77a"
            border.width: 2

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            text: "Authoring · Palace assets"
                            color: "#fff2cf"
                            font.bold: true
                            font.pixelSize: 18
                        }
                        Text {
                            text: root.authoringAssets.length
                                + " staged PNG"
                                + (root.authoringAssets.length === 1
                                   ? "" : "s")
                                + (root.canManageAssets
                                   ? " · approve, upload, then assign"
                                   : " · read-only catalog")
                            color: "#c9b78e"
                            font.pixelSize: 11
                        }
                        Text {
                            Layout.fillWidth: true
                            text: root.encodedStatusValue(
                                root.storageStatus, "storage")
                                === "running"
                                ? "Storage connected"
                                : "Start Storage in Logos Control, then connect it here."
                            color: root.encodedStatusValue(
                                root.storageStatus, "storage")
                                === "running" ? "#a7e3a0" : "#e2c37b"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        objectName: "palaceConnectStorage"
                        text: root.encodedStatusValue(
                            root.storageStatus, "storage") === "running"
                            ? "Storage connected" : "Connect Storage"
                        enabled: root.ready && root.canManageAssets
                            && root.encodedStatusValue(
                                root.storageStatus, "storage")
                                !== "running"
                        ToolTip.visible: hovered
                        ToolTip.text: "Start Storage in Logos Control first. Palace only connects to an already running node."
                        onClicked: root.connectStorage()
                    }

                    Button {
                        objectName: "palaceAssetSelectFile"
                        text: root.assetImportRunning
                            ? "Importing…" : "Add PNG…"
                        enabled: root.ready
                            && root.canManageAssets
                            && !root.assetImportRunning
                        onClicked: root.selectAssetFile()
                    }

                    Button {
                        objectName: "palaceBackgroundModerationClose"
                        text: "Close"
                        onClicked: root.backgroundModerationOpen = false
                    }
                }

                Rectangle {
                    objectName: "palaceRoomSetupPublication"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    color: "#2b2118"
                    border.color: "#8c7145"
                    radius: 4

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 7
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1

                            Text {
                                text: "Room setup"
                                color: "#fff2cf"
                                font.bold: true
                                font.pixelSize: 11
                            }
                            Text {
                                objectName: "palaceRoomSetupPublishStatus"
                                Layout.fillWidth: true
                                text: root.roomSetupPublishMessage()
                                color: root.canPublishRoomSetup
                                    ? "#a7e3a0"
                                    : (root.roomSetupPublishReadiness
                                       === "locked"
                                       ? "#f3cf8a" : "#d8a18f")
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        BusyIndicator {
                            Layout.preferredWidth: 22
                            Layout.preferredHeight: 22
                            running: root.roomSetupPublishReadiness
                                === "locked"
                            visible: running
                        }

                        Button {
                            objectName: "palacePublishRoomSetup"
                            text: "Publish room setup"
                            Accessible.name: text
                            visible: root.canManageAssets
                            enabled: root.canPublishRoomSetup
                            ToolTip.visible: hovered && !enabled
                            ToolTip.text: root.roomSetupPublishMessage()
                            onClicked: {
                                if (root.onboardingPhase === "authoring-rooms")
                                    root.publishRoomSetup()
                                else
                                    root.gate3PublishBundle()
                            }
                        }
                    }
                }

                RowLayout {
                    objectName: "palaceSharedStorageCatalogRow"
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.sharedStorageCatalog.length > 0

                    Text {
                        text: "Share catalog"
                        color: "#c9b78e"
                        font.pixelSize: 10
                    }

                    TextField {
                        objectName: "palaceSharedStorageCatalog"
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        maximumLength: 16384
                        text: root.sharedStorageCatalog
                        Accessible.name: "Shared room catalog"
                        ToolTip.visible: hovered
                        ToolTip.text: "Select and copy this catalog for Palace joiners."
                    }
                }

                RowLayout {
                    objectName: "palaceSharedStoragePeerRow"
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.sharedStoragePeerEndpoint.length > 0

                    Text {
                        text: "Share peer"
                        color: "#c9b78e"
                        font.pixelSize: 10
                    }

                    TextField {
                        objectName: "palaceSharedStoragePeerEndpoint"
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        maximumLength: 16384
                        text: root.sharedStoragePeerEndpoint
                        Accessible.name: "Shared Storage peer endpoint"
                        ToolTip.visible: hovered
                        ToolTip.text: "Select and copy this endpoint for Palace joiners."
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "Prop placement"
                        color: "#c9b78e"
                        font.pixelSize: 11
                    }

                    TextField {
                        objectName: "palaceAssetPropId"
                        Layout.preferredWidth: 110
                        placeholderText: "prop ID"
                        enabled: root.canManageAssets
                        text: root.propDraftId
                        onTextEdited: root.propDraftId = text
                    }

                    TextField {
                        objectName: "palaceAssetPropAnchorX"
                        Layout.preferredWidth: 76
                        placeholderText: "anchor X"
                        inputMethodHints: Qt.ImhDigitsOnly
                        enabled: root.canManageAssets
                        text: root.propDraftAnchorX
                        onTextEdited: root.propDraftAnchorX = text
                    }

                    TextField {
                        objectName: "palaceAssetPropAnchorY"
                        Layout.preferredWidth: 76
                        placeholderText: "anchor Y"
                        inputMethodHints: Qt.ImhDigitsOnly
                        enabled: root.canManageAssets
                        text: root.propDraftAnchorY
                        onTextEdited: root.propDraftAnchorY = text
                    }

                    TextField {
                        objectName: "palaceAssetPropLayer"
                        Layout.preferredWidth: 92
                        placeholderText: "layer"
                        enabled: root.canManageAssets
                        text: root.propDraftLayer
                        onTextEdited: root.propDraftLayer = text
                    }

                    RowLayout {
                        spacing: 3

                        Repeater {
                            model: ["head", "body", "hand", "back"]

                            delegate: Button {
                                objectName:
                                    "palaceAssetPropLayer-" + modelData
                                required property string modelData
                                text: modelData.slice(0, 1).toUpperCase()
                                    + modelData.slice(1)
                                font.pixelSize: 9
                                Layout.preferredWidth: 46
                                checkable: true
                                checked:
                                    root.propDraftLayer.trim() === modelData
                                enabled: root.canManageAssets
                                onClicked: root.propDraftLayer = modelData
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.canManageAssets
                            ? (root.propDraftReady()
                               ? "Ready to assign"
                               : "Pick layer, then click prop preview to set hot spot")
                            : root.assetAuthoringReadOnlyMessage()
                        color: root.canManageAssets
                            ? (root.propDraftReady()
                               ? "#a7e3a0" : "#8f826a")
                            : "#d8a18f"
                        elide: Text.ElideRight
                        font.pixelSize: 10
                    }
                }

                // Use a Flow/Repeater instead of GridView so every card and its
                // moderation controls stay instantiated for e2e discovery.
                // GridView recycling left later "Set Atrium/Lounge" buttons
                // unfindable after the first visible row was assigned.
                Flickable {
                    id: backgroundGrid
                    objectName: "palaceBackgroundGrid"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentWidth: width
                    contentHeight: backgroundFlow.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    flickableDirection: Flickable.VerticalFlick

                    Flow {
                        id: backgroundFlow
                        width: backgroundGrid.width
                        spacing: 10

                        Repeater {
                            model: root.authoringAssets

                            Rectangle {
                                id: backgroundCard
                                required property var modelData
                                property var asset: modelData || ({})
                                property string handle:
                                    String(asset.handle || "")
                                property string publicationState:
                                    String(asset.publicationState
                                           || "not-uploaded")
                                property var assignedRooms:
                                    Array.isArray(asset.roomAssignments)
                                    ? asset.roomAssignments : []
                                property var assignedProps:
                                    Array.isArray(asset.propAssignments)
                                    ? asset.propAssignments : []
                                property bool previewCounted: false
                                // Two wide cards keep all three assignment
                                // actions readable; four narrow cards elide
                                // their labels before they can be operated.
                                property int cardWidth: Math.max(
                                    320,
                                    Math.floor(
                                        (backgroundGrid.width
                                         - backgroundFlow.spacing) / 2))
                                width: cardWidth
                                height: 334
                                radius: 8
                                color: "#292018"
                                border.color: publicationState === "published"
                                    ? "#7ecb78" : "#62513b"

                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 5

                                    Image {
                                        id: backgroundPreview
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 112
                                        source: String(
                                            backgroundCard.asset.handle
                                            || "").length === 64
                                            ? "image://basecamp-verified/"
                                              + String(
                                                  backgroundCard.asset.handle)
                                            : ""
                                        fillMode: Image.PreserveAspectCrop
                                        smooth: true
                                        asynchronous: false
                                        onStatusChanged: {
                                            if (status === Image.Ready) {
                                                backgroundCard.previewCounted =
                                                    root.noteAuthoringPreviewReady(
                                                        backgroundCard
                                                            .previewCounted)
                                            } else {
                                                backgroundCard.previewCounted =
                                                    root.noteAuthoringPreviewLost(
                                                        backgroundCard
                                                            .previewCounted)
                                            }
                                            ++root.backgroundPreviewEpoch
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Text {
                                            Layout.fillWidth: true
                                            text: String(
                                                backgroundCard.asset.label
                                                || backgroundCard.handle)
                                            color: "#fff2cf"
                                            font.bold: true
                                            elide: Text.ElideRight
                                            font.pixelSize: 12
                                        }

                                        Text {
                                            visible:
                                                backgroundCard.assignedRooms
                                                    .length > 0
                                                || backgroundCard
                                                    .assignedProps.length > 0
                                            text: backgroundCard.assignedRooms
                                                .concat(
                                                    backgroundCard
                                                        .assignedProps)
                                                .join(" · ").toUpperCase()
                                            color: "#a7e3a0"
                                            font.bold: true
                                            font.pixelSize: 9
                                        }
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: Number(
                                            backgroundCard.asset.width)
                                            + "×"
                                            + Number(
                                                backgroundCard.asset.height)
                                            + " · "
                                            + String(
                                                backgroundCard.asset
                                                    .reviewState)
                                            + " · "
                                            + backgroundCard.publicationState
                                        color: "#c9b78e"
                                        elide: Text.ElideRight
                                        font.pixelSize: 10
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: backgroundCard.publicationState
                                               === "published"
                                               ? "CID "
                                                 + String(
                                                     backgroundCard.asset.cid
                                                     || "").slice(0, 14)
                                               : "SHA "
                                                 + backgroundCard.handle
                                                     .slice(0, 12)
                                        color: "#8f826a"
                                        elide: Text.ElideRight
                                        font.pixelSize: 9
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Button {
                                            objectName:
                                                "palaceAssetApprove-"
                                                + backgroundCard.handle
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 156
                                            text: backgroundCard
                                                    .publicationState
                                                    === "published"
                                                ? "Uploaded"
                                                : "Approve & upload"
                                            enabled: root.ready
                                                && root.canManageAssets
                                                && backgroundCard
                                                    .publicationState
                                                    !== "published"
                                                && backgroundCard.handle
                                                    .length > 0
                                            onClicked:
                                                root.reviewAndPublishAsset(
                                                    backgroundCard.handle)
                                        }

                                        Button {
                                            objectName:
                                                "palaceAssetReject-"
                                                + backgroundCard.handle
                                            Layout.minimumWidth: 84
                                            text: "Reject"
                                            enabled: root.ready
                                                && root.canManageAssets
                                                && backgroundCard
                                                    .publicationState
                                                    !== "published"
                                            onClicked:
                                                root.reviewAsset(
                                                    backgroundCard.handle,
                                                    "reject")
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true

                                        Button {
                                            objectName:
                                                "palaceBackgroundAssignAtrium-"
                                                + backgroundCard.handle
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 96
                                            text: "Set Atrium"
                                            enabled: root.ready
                                                && root.canManageAssets
                                                && backgroundCard
                                                    .publicationState
                                                    === "published"
                                                && backgroundCard.handle
                                                    .length === 64
                                            onClicked:
                                                root.assignRoomBackground(
                                                    "atrium",
                                                    backgroundCard.handle)
                                        }

                                        Button {
                                            objectName:
                                                "palaceAssetAssignProp-"
                                                + backgroundCard.handle
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 96
                                            text: "Set prop"
                                            enabled: root.ready
                                                && root.canManageAssets
                                                && backgroundCard
                                                    .publicationState
                                                    === "published"
                                                && backgroundCard.handle
                                                    .length === 64
                                                && root.propDraftReady()
                                            onClicked:
                                                root.assignPropAsset(
                                                    root.propDraftId.trim(),
                                                    backgroundCard.handle,
                                                    root.parsedAssetAnchor(
                                                        root.propDraftAnchorX),
                                                    root.parsedAssetAnchor(
                                                        root.propDraftAnchorY),
                                                    root.propDraftLayer.trim())
                                        }

                                        Button {
                                            objectName:
                                                "palaceBackgroundAssignLounge-"
                                                + backgroundCard.handle
                                            Layout.fillWidth: true
                                            Layout.minimumWidth: 96
                                            text: "Set Lounge"
                                            enabled: root.ready
                                                && root.canManageAssets
                                                && backgroundCard
                                                    .publicationState
                                                    === "published"
                                                && backgroundCard.handle
                                                    .length === 64
                                            onClicked:
                                                root.assignRoomBackground(
                                                    "lounge",
                                                    backgroundCard.handle)
                                        }
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        visible: backgroundCard
                                            .publicationState
                                            === "published"
                                            && backgroundCard.handle.length
                                            === 64

                                        Rectangle {
                                            id: propPlacementStage
                                            objectName:
                                                "palaceAssetPropPreview-"
                                                + backgroundCard.handle
                                            property real sourceWidth:
                                                Math.max(
                                                    1,
                                                    Number(backgroundCard.asset
                                                        .width))
                                            property real sourceHeight:
                                                Math.max(
                                                    1,
                                                    Number(backgroundCard.asset
                                                        .height))
                                            property real previewScale:
                                                root.propPreviewScale(
                                                    sourceWidth,
                                                    sourceHeight)
                                            property real targetX:
                                                root.propPreviewTargetX(
                                                    root.propDraftLayer,
                                                    width)
                                            property real targetY:
                                                root.propPreviewTargetY(
                                                    root.propDraftLayer,
                                                    height)
                                            width: 152
                                            height: 86
                                            radius: 5
                                            color: "#18110c"
                                            border.color:
                                                root.propDraftReady()
                                                ? "#7ecb78" : "#62513b"

                                            Rectangle {
                                                width: 34
                                                height: 34
                                                radius: 17
                                                color: "#ffe566"
                                                border.color: "#222222"
                                                border.width: 2
                                                anchors.horizontalCenter:
                                                    parent.horizontalCenter
                                                anchors.verticalCenter:
                                                    parent.verticalCenter
                                            }

                                            Rectangle {
                                                width: 26
                                                height: 18
                                                radius: 7
                                                color: "#ffe566"
                                                border.color: "#222222"
                                                border.width: 2
                                                anchors.horizontalCenter:
                                                    parent.horizontalCenter
                                                anchors.top:
                                                    parent.verticalCenter
                                                anchors.topMargin: 8
                                            }

                                            Item {
                                                id: propPlacementPreview
                                                width: propPlacementStage
                                                    .sourceWidth
                                                    * propPlacementStage
                                                        .previewScale
                                                height: propPlacementStage
                                                    .sourceHeight
                                                    * propPlacementStage
                                                        .previewScale
                                                x: propPlacementStage.targetX
                                                    - root.clampAnchorToAsset(
                                                        root.parsedAssetAnchor(
                                                            root.propDraftAnchorX),
                                                        propPlacementStage
                                                            .sourceWidth)
                                                      * propPlacementStage
                                                            .previewScale
                                                y: propPlacementStage.targetY
                                                    - root.clampAnchorToAsset(
                                                        root.parsedAssetAnchor(
                                                            root.propDraftAnchorY),
                                                        propPlacementStage
                                                            .sourceHeight)
                                                      * propPlacementStage
                                                            .previewScale
                                                z: root.propDraftLayer.trim()
                                                    === "back" ? 0 : 2

                                                Image {
                                                    anchors.fill: parent
                                                    source:
                                                        "image://basecamp-verified/"
                                                        + backgroundCard.handle
                                                    fillMode: Image.Stretch
                                                    smooth: true
                                                    asynchronous: false
                                                    opacity: root
                                                        .propDraftReady()
                                                        ? 0.95 : 0.72
                                                }

                                                Rectangle {
                                                    width: 8
                                                    height: 8
                                                    radius: 4
                                                    color: "#ffcc44"
                                                    border.color: "#3b2c14"
                                                    border.width: 1
                                                    x: root.clampAnchorToAsset(
                                                        root.parsedAssetAnchor(
                                                            root.propDraftAnchorX),
                                                        propPlacementStage
                                                            .sourceWidth)
                                                       * propPlacementStage
                                                            .previewScale
                                                       - width / 2
                                                    y: root.clampAnchorToAsset(
                                                        root.parsedAssetAnchor(
                                                            root.propDraftAnchorY),
                                                        propPlacementStage
                                                            .sourceHeight)
                                                       * propPlacementStage
                                                            .previewScale
                                                       - height / 2
                                                }

                                                MouseArea {
                                                    objectName:
                                                        "palaceAssetPropPreviewHit-"
                                                        + backgroundCard.handle
                                                    anchors.fill: parent
                                                    enabled: root.canManageAssets
                                                    cursorShape: enabled
                                                        ? Qt.CrossCursor
                                                        : Qt.ArrowCursor
                                                    onClicked:
                                                        root.setPropDraftAnchorFromPreview(
                                                            mouse.x,
                                                            mouse.y,
                                                            propPlacementStage
                                                                .previewScale,
                                                            propPlacementStage
                                                                .sourceWidth,
                                                            propPlacementStage
                                                                .sourceHeight)
                                                }
                                            }

                                            Rectangle {
                                                width: 14
                                                height: 14
                                                radius: 7
                                                color: "#ffcc44"
                                                border.color: "#3b2c14"
                                                border.width: 1
                                                x: propPlacementStage.targetX
                                                    - width / 2
                                                y: propPlacementStage.targetY
                                                    - height / 2
                                                z: 3
                                            }
                                        }

                                        ColumnLayout {
                                            Layout.fillWidth: true
                                            spacing: 3

                                            Text {
                                                Layout.fillWidth: true
                                                text: "Prop preview"
                                                color: "#fff2cf"
                                                font.bold: true
                                                font.pixelSize: 10
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                wrapMode: Text.Wrap
                                                text: root.propDraftReady()
                                                    ? "Hot spot follows yellow pin."
                                                    : "Set prop ID/layer, then click prop to place hot spot."
                                                color: "#c9b78e"
                                                font.pixelSize: 9
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: "Anchor "
                                                    + root.propDraftAnchorX
                                                    + ","
                                                    + root.propDraftAnchorY
                                                    + " · "
                                                    + (root.propDraftLayer
                                                        .length > 0
                                                       ? root.propDraftLayer
                                                       : "layer?")
                                                color: "#a7e3a0"
                                                font.pixelSize: 9
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }

                                Component.onDestruction: {
                                    previewCounted =
                                        root.noteAuthoringPreviewLost(
                                            previewCounted)
                                }
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.canManageAssets
                        ? (root.lastActionReceipt.length > 0
                           ? root.lastActionReceipt
                           : "Draft assignment becomes authority when Palace creation finalizes.")
                        : root.assetAuthoringReadOnlyMessage()
                    color: root.canManageAssets
                        ? (root.lastActionReceipt.indexOf("rejected=") === 0
                           ? "#ff9c8f" : "#a7e3a0")
                        : "#d8a18f"
                    elide: Text.ElideRight
                    font.pixelSize: 10
                }
            }
        }
        } // backgroundModerationComponent

        // Stable inspector alias for pre-layout action dock (now input strip).
        Item {
            objectName: "palaceActionDock"
            visible: false
            width: 0
            height: 0
        }

        // Compact diagnostic strip — not a dense panel over the room (classic
        // client keeps diagnostics out of the View Screen).
        Rectangle {
            objectName: "palaceDeliveryStatus"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 2
            width: Math.min(parent.width - 4, 420)
            height: 16
            color: "#d4d0c8"
            border.color: "#808080"
            z: 15

            Text {
                id: deliveryStatusText
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.pixelSize: 9
                color: "#222222"
                text: (root.ready
                       ? "Delivery " + (root.statusValue("state") || "offline")
                       : "Connecting")
                    + " · peers " + root.connectedPeerCount
                    + " · sync " + root.syncDisplayState
                    + " · Storage " + root.storageDisplayState
                    + " · LEZ "
                    + (root.encodedStatusValue(root.lezState, "ready") === "1"
                       ? "ok" : "off")
                    + " · Palace "
                    + (root.encodedStatusValue(root.palaceState, "palace")
                       || "closed")
                    + (root.localDevelopmentMode
                       ? " · local development"
                       : "")
                    + " · door " + (root.gate5VmPhase || "idle")
            }
        }

        Rectangle {
            id: onboardingOverlay
            objectName: "palaceOnboardingOverlay"
            anchors.fill: parent
            color: "#10141de8"
            z: 100
            visible: !root.roomUsable
                && !(root.onboardingPhase === "authoring-rooms"
                     && root.backgroundModerationOpen)
            enabled: visible

            onVisibleChanged: {
                if (!visible || !root.ready)
                    return
                if (root.onboardingPhase === "authoring-rooms")
                    onboardingPalaceTitleInput.forceActiveFocus()
                else
                    onboardingPasswordInput.forceActiveFocus()
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                }
            }

            Rectangle {
                id: onboardingCard
                objectName: "palaceOnboardingCard"
                anchors.centerIn: parent
                width: Math.min(parent.width - 32, 540)
                height: onboardingContent.implicitHeight + 36
                color: "#f5f0e6"
                border.color: "#a88451"
                border.width: 2
                radius: 8
                z: 1

                ColumnLayout {
                    id: onboardingContent
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 9

                    Text {
                        Layout.fillWidth: true
                        text: "Open a Palace"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 23
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Configure and start network nodes in Logos Control. Palace uses the running modules but does not manage them."
                        color: "#463b2d"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "Use a Palace address to open an existing Palace, or leave it blank to create one after your room setup is published."
                        color: "#463b2d"
                        font.pixelSize: 13
                        wrapMode: Text.Wrap
                    }

                    Text {
                        text: "LEZ password"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    TextField {
                        id: onboardingPasswordInput
                        objectName: "palaceOnboardingLezPassword"
                        Layout.fillWidth: true
                        placeholderText: "Password for LEZ on this device"
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhNoPredictiveText
                            | Qt.ImhSensitiveData
                        maximumLength: 1024
                        text: root.onboardingPassword
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                        Accessible.name: "LEZ password"
                        onTextEdited: {
                            root.onboardingPassword = text
                            root.resetOnboardingAfterEdit()
                        }
                    }

                    Text {
                        text: "Display name"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    TextField {
                        id: onboardingDisplayNameInput
                        objectName: "palaceOnboardingDisplayName"
                        Layout.fillWidth: true
                        placeholderText: "Name shown in the Palace"
                        maximumLength: 48
                        text: root.onboardingDisplayName
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                        Accessible.name: "Display name"
                        onTextEdited: {
                            root.onboardingDisplayName = text
                            root.resetOnboardingAfterEdit()
                        }
                    }

                    Text {
                        text: "Existing Palace address (optional)"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    TextField {
                        id: onboardingPalaceAddressInput
                        objectName: "palaceOnboardingPalaceAddress"
                        Layout.fillWidth: true
                        placeholderText: "palace://existing-palace-id"
                        maximumLength: 80
                        text: root.onboardingPalaceAddress
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                        Accessible.name: "Palace address"
                        onTextEdited: {
                            root.onboardingPalaceAddress = text
                            root.resetOnboardingAfterEdit()
                        }
                        onAccepted: root.activateOnboarding()
                    }

                    Text {
                        text: "Shared room catalog (required for an existing Palace)"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 12
                        visible: root.onboardingUsesExistingPalace
                    }

                    TextField {
                        id: onboardingStorageCatalogInput
                        objectName: "palaceOnboardingStorageCatalog"
                        Layout.fillWidth: true
                        placeholderText: "Paste the catalog value shared by the Palace creator"
                        maximumLength: 16384
                        text: root.onboardingStorageCatalog
                        visible: root.onboardingUsesExistingPalace
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                        Accessible.name: "Shared room catalog"
                        onTextEdited: {
                            root.onboardingStorageCatalog = text
                            root.resetOnboardingAfterEdit()
                        }
                    }

                    Text {
                        text: "Storage peer endpoint (required for an existing Palace)"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 12
                        visible: root.onboardingUsesExistingPalace
                    }

                    TextField {
                        id: onboardingStoragePeerEndpointInput
                        objectName: "palaceOnboardingStoragePeerEndpoint"
                        Layout.fillWidth: true
                        placeholderText: "Paste the endpoint shared by the Palace creator"
                        maximumLength: 16384
                        text: root.onboardingStoragePeerEndpoint
                        visible: root.onboardingUsesExistingPalace
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                        Accessible.name: "Storage peer endpoint"
                        onTextEdited: {
                            root.onboardingStoragePeerEndpoint = text
                            root.resetOnboardingAfterEdit()
                        }
                    }

                    Text {
                        text: "New Palace title"
                        color: "#2b2016"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    TextField {
                        id: onboardingPalaceTitleInput
                        objectName: "palaceOnboardingPalaceTitle"
                        Layout.fillWidth: true
                        placeholderText: "Name for a new Palace"
                        text: root.onboardingPalaceTitle
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                        Accessible.name: "New Palace title"
                        onTextEdited: {
                            root.onboardingPalaceTitle = text
                            root.resetOnboardingAfterEdit()
                        }
                        onAccepted: root.activateOnboarding()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 42
                        color: root.onboardingPhase === "error"
                            ? "#f9dedc" : "#e7edf3"
                        border.color: root.onboardingPhase === "error"
                            ? "#b54a43" : "#8197ad"
                        radius: 4

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 7
                            spacing: 7

                            BusyIndicator {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                running: root.onboardingWorking
                                    || root.onboardingPhase
                                        === "waiting-palace"
                                    || root.onboardingPhase
                                        === "waiting-created-palace"
                                visible: running
                            }

                            Text {
                                Layout.fillWidth: true
                                text: root.onboardingProgressText()
                                color: root.onboardingPhase === "error"
                                    ? "#741c18" : "#24394d"
                                font.pixelSize: 12
                                wrapMode: Text.Wrap
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    Button {
                        objectName: "palaceOnboardingOpenButton"
                        Layout.alignment: Qt.AlignRight
                        text: !root.ready
                            ? "Connecting…"
                            : (root.onboardingWorking
                               ? "Working…"
                               : (root.onboardingPhase === "waiting-palace"
                                  || root.onboardingPhase
                                     === "waiting-created-palace"
                                  ? "Opening Palace…"
                                  : (root.onboardingPhase
                                     === "authoring-rooms"
                                     ? "Open Assets"
                                     : (root.onboardingResumeReady
                                        ? (root.onboardingUsesExistingPalace
                                           ? "Open Palace"
                                           : "Continue room setup")
                                     : (root.onboardingPhase === "error"
                                        ? "Try again"
                                        : (root.onboardingUsesExistingPalace
                                           ? "Open Palace"
                                           : "Start room setup"))))))
                        enabled: root.ready && !root.onboardingWorking
                            && root.onboardingPhase !== "waiting-palace"
                            && root.onboardingPhase
                                !== "waiting-created-palace"
                            && (root.onboardingPhase === "authoring-rooms"
                                || root.onboardingPhase === "error"
                                || root.onboardingInputReady())
                        Accessible.name: text
                        onClicked: root.activateOnboarding()
                    }
                }
            }
        }
}
