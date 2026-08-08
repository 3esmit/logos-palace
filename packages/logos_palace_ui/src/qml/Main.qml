pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "admin"
import "components"
import "onboarding"
import "room"

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
    readonly property string durableActionStatus: backend
        ? backend.durableActionStatus
        : "state=idle;action=none;durable=none"
    readonly property string storageBundleWorkflowStatus: backend
        ? backend.storageBundleWorkflowStatus : "state=idle"
    readonly property string palaceRegistrationWorkflowStatus: backend
        ? backend.palaceRegistrationWorkflowStatus
        : "state=idle;action=none;registration=none"
    readonly property string onboardingWorkflowStatus: backend
        ? backend.onboardingWorkflowStatus : "state=idle"
    readonly property string participantProjection: backend
        ? backend.participantProjection : "[]"
    readonly property string deliveryNodeStatus: backend
        ? backend.deliveryNodeStatus
        : "{\"success\":false,\"reason\":\"node-not-created\"}"
    property string invocationError: ""
    // Local copy of the latest successful watchAction value. Prefer this over
    // backend.lastActionReceipt alone so asynchronous UI updates preserve the
    // receipt even if remote-object property propagation lags the sequence bump.
    property string watchedActionReceipt: ""
    property int invocationSequence: 0
    readonly property string lastActionReceipt: invocationError.length > 0
        ? invocationError
        : (watchedActionReceipt.length > 0
           ? watchedActionReceipt
           : (backend ? backend.lastActionReceipt : ""))

    // Compact status surface for delivery actions and state.
    readonly property string deliveryReceipt: lastActionReceipt
    readonly property int participantCount: participants.length
    readonly property string storageReceipt: lastActionReceipt
    readonly property string storageRoomHandle: roomBackgroundHandle
    readonly property string storageActivePropAsset: activePropAssetState
    readonly property bool storageReady: ready
    readonly property string doorAction: spotActionId
    readonly property string doorStatus: spotState
    readonly property string doorRoomTitle: roomTitle
    readonly property string doorReceipt: invocationError.length > 0
        ? invocationError : spotReceipt
    readonly property bool doorReady: ready
    readonly property string doorVmPhase:
        encodedStatusValue(spotState, "vm")
    readonly property bool doorBlocked:
        spotActionId.length > 0
        && doorVmPhase !== "promoted"
        && doorVmPhase !== "idle"
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
        && (onboardingMode === "recover"
            ? (onboardingPalaceAddress.trim().length > 0
                && onboardingStorageCatalog.trim().length > 0)
            : (onboardingMode !== "join"
               || onboardingInvitation.trim().length > 0
               || (onboardingPalaceAddress.trim().length > 0
                   && onboardingStorageCatalog.trim().length > 0
                   && onboardingStoragePeerEndpoint.trim().length > 0)))

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
        onboardingPublishedStorageCatalog.length > 0
            ? onboardingPublishedStorageCatalog
            : encodedStatusValue(onboardingBundleStatus, "catalog")
    readonly property string sharedStoragePeerEndpoint:
        onboardingStoragePeerEndpoint
    readonly property string sharedPalaceAddress: {
        var palaceId = encodedStatusValue(palaceState, "id")
        return /^[0-9a-f]{64}$/.test(palaceId)
            ? "palace://" + palaceId : onboardingCreatedPalaceUri
    }
    readonly property string sharedPalaceInvitation:
        sharedPalaceAddress.length > 0
            && sharedStorageCatalog.length > 0
            && sharedStoragePeerEndpoint.length > 0
        ? JSON.stringify({
            "palace": sharedPalaceAddress,
            "storageCatalog": sharedStorageCatalog,
            "storagePeerEndpoint": sharedStoragePeerEndpoint
        }) : ""
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
        parseConnectedPeerCount(deliveryNodeStatus)
    property bool ready: false
    property string onboardingPassword: ""
    property string onboardingDisplayName: ""
    property string onboardingPalaceAddress: ""
    property string onboardingInvitation: ""
    property string onboardingMode: "choose"
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
    property string onboardingPublishedStorageCatalog: ""
    property string onboardingCreatedPalaceUri: ""
    property string onboardingCreatorActionId: "0"
    readonly property bool onboardingUsesExistingPalace:
        onboardingMode === "join" || onboardingMode === "recover"
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
    property bool adminDrawerOpen: false
    property int backgroundPreviewEpoch: 0
    property int backgroundReadyImageCount: 0
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
    property string assetImportStatus: ""
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
            roomChatBar.focusInput()
    }

    function closeActiveUtilityPanel() {
        if (adminDrawerOpen) {
            adminDrawerOpen = false
            return true
        }
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
        if (onboardingMode !== "create" && onboardingMode !== "join"
                && onboardingMode !== "recover")
            return false
        return onboardingPassword.length > 0
            && onboardingDisplayName.trim().length > 0
            && (onboardingUsesExistingPalace
                || onboardingPalaceTitle.trim().length > 0)
            && (onboardingMode === "recover"
                ? (onboardingPalaceAddress.trim().length > 0
                    && onboardingStorageCatalog.trim().length > 0)
                : (onboardingMode !== "join"
                   || onboardingInvitation.trim().length > 0
                   || (!onboardingUsesExistingPalace
                   || (onboardingPalaceAddress.trim().length > 0
                       && onboardingStorageCatalog.trim().length > 0
                       && onboardingStoragePeerEndpoint.trim().length > 0))))
    }

    function clearOnboardingPassword() {
        onboardingPassword = ""
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
            if (result.indexOf("palace-invitation-invalid") >= 0)
                return "Invitation is invalid. Ask the Palace creator for a new one."
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

    function applyOnboardingInvitation() {
        try {
            var invitation = JSON.parse(onboardingInvitation.trim())
            var palace = String(
                invitation.palace || invitation.palaceAddress || "")
            var catalog = String(
                invitation.storageCatalog || invitation.catalog || "")
            var peer = String(
                invitation.storagePeerEndpoint || invitation.peer || "")
            if (!/^palace:\/\/[0-9a-f]{64}$/.test(palace)
                    || catalog.length === 0 || peer.length === 0
                    || parseStoragePeerEndpoint(peer) === null)
                return false
            onboardingPalaceAddress = palace
            onboardingStorageCatalog = catalog
            onboardingStoragePeerEndpoint = peer
            return true
        } catch (error) {
            return false
        }
    }

    function onboardingPreparationFailureStep(receipt) {
        var status = String(receipt)
        if (status.indexOf("invalid-palace-uri") >= 0)
            return "palace"
        if (status.indexOf("storage-peer") >= 0)
            return "storage-peer"
        if (status.indexOf("storage-catalog") >= 0)
            return "catalog"
        if (status.indexOf("storage-") >= 0)
            return "storage"
        if (status.indexOf("identity-") >= 0)
            return "identity"
        if (status.indexOf("lez-") >= 0)
            return "lez"
        return onboardingUsesExistingPalace ? "catalog" : "identity"
    }

    function refreshSharedStoragePeerEndpoint() {
        watchAction(backend.storagePeerEndpoint(), function (receipt) {
            if (String(receipt).indexOf("rejected=") !== 0)
                onboardingStoragePeerEndpoint = String(receipt)
        }, null)
    }

    function enterCreatorMode() {
        onboardingMode = "create"
        clearOnboardingPassword()
        onboardingFailureStep = ""
        onboardingError = ""
        onboardingPhase = "authoring-rooms"
        backgroundModerationOpen = true
    }

    function resumeOnboardingWorkflow() {
        return watchAction(
            backend.resumePalaceOnboarding(),
            function (receipt) {
                onboardingRecordSuccess(receipt)
            },
            function (receipt) {
                onboardingFail(
                    onboardingUsesExistingPalace ? "catalog" : "finality",
                    receipt)
            })
    }

    function resumeExistingPalaceWorkflow() {
        var peer = parseStoragePeerEndpoint(
            onboardingStoragePeerEndpoint)
        if (onboardingPalaceAddress.trim().length === 0
                || onboardingStorageCatalog.trim().length === 0
                || (onboardingMode === "join" && peer === null)) {
            onboardingFail(
                onboardingMode === "join" ? "storage-peer" : "catalog",
                "rejected=onboarding-recovery-input-required")
            return "rejected=onboarding-recovery-input-required"
        }
        return watchAction(
            backend.resumeExistingPalace(
                onboardingPalaceAddress.trim(),
                onboardingStorageCatalog.trim(),
                peer === null ? "" : peer.peerId,
                peer === null ? "" : peer.addressesJson,
                onboardingMode === "join"),
            function (receipt) {
                onboardingRecordSuccess(receipt)
            },
            function (receipt) {
                onboardingFail("catalog", receipt)
            })
    }

    function startOnboarding() {
        if (!ready || !backend) {
            onboardingPhase = "error"
            onboardingError =
                "Palace is still starting. Try again in a moment."
            return "rejected=ui-not-ready"
        }
        if (onboardingMode !== "create" && onboardingMode !== "join"
                && onboardingMode !== "recover") {
            onboardingPhase = "details"
            onboardingError = "Choose Create, Join, or Recover Palace first."
            return "rejected=onboarding-mode-required"
        }
        if (onboardingMode === "join"
                && onboardingInvitation.trim().length > 0
                && !applyOnboardingInvitation()) {
            onboardingFail(
                "catalog", "rejected=palace-invitation-invalid")
            return "rejected=palace-invitation-invalid"
        }
        if (onboardingResumeReady) {
            onboardingError = ""
            onboardingReceipt = ""
            onboardingFailureStep = ""
            if (onboardingUsesExistingPalace)
                return resumeExistingPalaceWorkflow()
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
        var onboardingPeer = parseStoragePeerEndpoint(
            onboardingStoragePeerEndpoint)
        var onboardingPeerId = onboardingPeer === null
            ? "" : onboardingPeer.peerId
        var onboardingPeerAddresses = onboardingPeer === null
            ? "" : onboardingPeer.addressesJson
        onboardingPhase = "starting-lez"
        return watchAction(
            backend.preparePalaceOnboarding(
                String(onboardingMode),
                String(onboardingPassword),
                onboardingDisplayName.trim(),
                onboardingPalaceAddress.trim(),
                onboardingStorageCatalog.trim(),
                onboardingMode === "join" ? onboardingPeerId : "",
                onboardingMode === "join"
                    ? onboardingPeerAddresses : ""),
            function (receipt) {
                onboardingRecordSuccess(receipt)
                if (!onboardingUsesExistingPalace) {
                    enterCreatorMode()
                }
            },
            function (receipt) {
                onboardingFail(
                    onboardingPreparationFailureStep(receipt), receipt)
            })
    }

    function publishRoomSetup() {
        if (onboardingPhase !== "authoring-rooms")
            return storagePublishBundle()
        if (!canPublishRoomSetup) {
            onboardingError = roomSetupPublishMessage()
            return "rejected=room-setup-not-ready"
        }
        onboardingError = ""
        onboardingPhase = "publishing-room-setup"
        backgroundModerationOpen = false
        return publishRoomSetupBundle(function (receipt) {
            onboardingBundleStatus = receipt
            createOnboardingPalace()
        }, function (receipt) {
            onboardingFail("bundle", receipt)
        })
    }

    function createOnboardingPalace() {
        var title = onboardingPalaceTitle.trim()
        if (title.length === 0) {
            onboardingFail("create", "rejected=palace-title-empty")
            return "rejected=palace-title-empty"
        }
        onboardingPhase = "checking-room-setup"
        onboardingError = ""
        return watchAction(
            backend.beginPalaceCreation(title),
            function (receipt) {
                onboardingRecordSuccess(receipt)
            },
            function (receipt) {
                onboardingFail("create", receipt)
            })
    }

    function onboardingWorkflowFailureStep(receipt) {
        var status = String(receipt)
        var phase = encodedStatusValue(onboardingWorkflowStatus, "state")
        if (phase.indexOf("storage") >= 0
                || status.indexOf("storage-") >= 0)
            return "catalog"
        if (phase.indexOf("palace-identity") >= 0
                || status.indexOf("identity-") >= 0)
            return "palace-identity"
        if (phase.indexOf("opening") >= 0)
            return "created-palace"
        if (phase.indexOf("creation") >= 0
                || phase.indexOf("room-state") >= 0)
            return "finality"
        return onboardingUsesExistingPalace ? "catalog" : "create"
    }

    function updateOnboardingWorkflow(receipt) {
        var status = String(receipt)
        onboardingReceipt = status
        onboardingBundleStatus = status
        var catalog = encodedStatusValue(status, "catalog")
        if (catalog.length > 0)
            onboardingPublishedStorageCatalog = catalog
        if (status.indexOf("rejected=") === 0) {
            onboardingFail(onboardingWorkflowFailureStep(status), status)
            return
        }

        var phase = encodedStatusValue(status, "state")
        if (phase.length === 0 || phase === "idle")
            return
        onboardingPhase = phase
        onboardingError = ""
        var palaceUri = encodedStatusValue(status, "palace_uri")
        if (/^palace:\/\/[0-9a-f]{64}$/.test(palaceUri))
            onboardingCreatedPalaceUri = palaceUri
        var action = encodedStatusValue(status, "action")
        if (/^[0-9]+$/.test(action))
            onboardingCreatorActionId = action
        if (phase === "complete") {
            onboardingFailureStep = ""
            clearOnboardingPassword()
            if (!onboardingUsesExistingPalace)
                refreshSharedStoragePeerEndpoint()
        }
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
            if (onboardingFailureStep === "create"
                    || onboardingFailureStep === "finality"
                    || onboardingFailureStep === "created-palace"
                    || onboardingFailureStep === "palace-identity"
                    || onboardingFailureStep === "catalog")
                return resumeOnboardingWorkflow()
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
                && onboardingReceipt.indexOf(";rejected=") >= 0) {
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

    function onboardingButtonText() {
        if (!ready)
            return "Connecting…"
        if (onboardingMode === "choose")
            return "Choose Create or Join"
        if (onboardingWorking)
            return "Working…"
        if (onboardingPhase === "waiting-palace"
                || onboardingPhase === "waiting-created-palace")
            return "Opening Palace…"
        if (onboardingPhase === "authoring-rooms")
            return "Open Assets"
        if (onboardingResumeReady)
            return onboardingUsesExistingPalace
                ? "Open Palace" : "Continue room setup"
        if (onboardingPhase === "error")
            return "Try again"
        return onboardingUsesExistingPalace
            ? "Open Palace" : "Start room setup"
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
        ++backgroundPreviewEpoch
    }

    function parseConnectedPeerCount(encoded) {
        try {
            var nodeStatus = JSON.parse(encoded)
            if (!nodeStatus || nodeStatus.success !== true)
                return 0
            var peers = nodeStatus.connectedPeers
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

    function ensureAdminControlVisible(objectName) {
        var name = String(objectName || "")
        var content = findNamedDescendant(root, "palaceAdminContent")
        var control = findNamedDescendant(root, name)
        if (name.length === 0 || !content || !control)
            return "missing"
        var contentItem = content.contentItem
        if (!contentItem)
            return "missing-content"
        var point = control.mapToItem(
            contentItem, control.width / 2, control.height / 2)
        var margin = 16
        var top = point.y - control.height / 2 - margin
        var bottom = point.y + control.height / 2 + margin
        var viewTop = content.contentY
        var viewBottom = viewTop + content.height
        if (top < viewTop) {
            content.contentY = Math.max(0, top)
        } else if (bottom > viewBottom) {
            content.contentY = Math.min(
                Math.max(0, content.contentHeight - content.height),
                bottom - content.height)
        }
        return "ok"
    }

    function roomX(coordinate) {
        var usableWidth = Math.max(
            1, roomView.width - roomCanvasHorizontalInset * 2)
        return roomCanvasHorizontalInset
            + clampCoordinate(coordinate) * usableWidth / 10000
    }

    function roomY(coordinate) {
        var usableHeight = Math.max(
            1, roomView.height - roomCanvasVerticalInset)
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
                Math.max(1, roomView.width
                         - roomCanvasHorizontalInset * 2)),
            "y": canvasPixelToProtocol(
                pixelY, roomCanvasTopInset,
                Math.max(1, roomView.height - roomCanvasVerticalInset))
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

    function roomEnterRoom(roomId) {
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
            return root.useDoor()
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

    function storageStartStorage(configJson) {
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

    function storageFetchPng(sourceCid, derivativeCid, byteLength,
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

    function storageAssetStatus(derivativeCid) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.assetStatus(String(derivativeCid)), null)
    }

    function storagePublishPng(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        return watchAction(
            backend.publishVerifiedPng(String(handle)), null)
    }

    function storagePublicationStatus(handle) {
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

    function reviewAndPublishAsset(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        if (!canManageAssets)
            return rejectAssetAuthoringReadOnly()
        invocationError = ""
        return watchAction(
            backend.approveAndPublishAsset(String(handle)), null)
    }

    function safeAssetStageLabel(value) {
        var candidate = String(value || "")
        // Thirty-two UTF-16 code units fit Core's 128-byte UTF-8 label bound.
        // A display name is never authority, so use a generic local label when
        // a bridge result is longer or contains a control character.
        if (candidate.length === 0 || candidate.length > 32
                || /[\u0000-\u001f\u007f]/.test(candidate))
            return "selected-image"
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
        assetImportStatus = JSON.stringify(completed)
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
        if (!backend)
            return
        logos.watch(
            backend.cancelAssetImport(),
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
            backend.beginAssetImport(
                label, assetImportTrace.byteLength),
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
            backend.finishAssetImport(),
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
            backend.appendAssetImportChunk(base64),
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
        assetImportStatus = ""
        invocationError = ""
        return "pending"
    }

    function storagePublishBundle() {
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

    function storageBundleStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.mvpStorageBundleStatus(), null)
    }

    function storageFetchBundle(catalogBase64) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.fetchMvpStorageBundle(String(catalogBase64)), null)
    }

    function storageVerifyRetention() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.verifyMvpStorageRetention(), null)
    }

    function storageObjectStatus(objectId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.storageObjectStatus(String(objectId)), null)
    }

    function storageStorageStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.storageSessionStatus(), null)
    }

    function storageStoragePeerEndpoint() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.storagePeerEndpoint(), null)
    }

    function storageConnectStoragePeer(peerId, addressesJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.connectStoragePeer(
                String(peerId), String(addressesJson)),
            null)
    }

    function storageMarkStorageMaterialized() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.markStorageMaterialized(), null)
    }

    function banUser(subjectUserIdHex) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.banUser(String(subjectUserIdHex)), null)
    }

    function banProp(propId) {
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

    function useDoor() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.useSpot("door"), null)
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

    Component.onDestruction: root.abandonAssetImport()

    onBackgroundModerationOpenChanged: {
        if (!backgroundModerationOpen) {
            resetAuthoringPreviewState()
            focusChatWhenUnobstructed()
        }
    }

    onAdminDrawerOpenChanged: {
        if (!adminDrawerOpen)
            focusChatWhenUnobstructed()
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

    onOnboardingWorkflowStatusChanged: {
        updateOnboardingWorkflow(onboardingWorkflowStatus)
    }

    onRoomUsableChanged: {
        if (!roomUsable) {
            adminDrawerOpen = false
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
            && (root.adminDrawerOpen || root.backgroundModerationOpen
            || root.propBagOpen
            || root.roomListOpen || root.userListOpen
                )
        onActivated: root.closeActiveUtilityPanel()
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

            RoomToolbar {
                app: root
            }

            RoomView {
                id: roomView
                app: root
            }



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
                        onClicked: root.banUser(subjectUserId)
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
                            root.banProp(root.availablePropId)
                        }
                    }
                }
            }

            ChatBar {
                id: roomChatBar
                app: root
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

        RoomUtilityPanels {
            app: root
        }

        // Stable inspector alias for pre-layout action dock (now input strip).
        Item {
            objectName: "palaceActionDock"
            visible: false
            width: 0
            height: 0
        }

        ConnectionStatus {
            objectName: "palaceDeliveryStatus"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 2
            width: Math.min(parent.width - 4, 420)
            z: 15
            ready: root.ready
            peerCount: root.connectedPeerCount
            deliveryState: root.statusValue("state") || "offline"
            syncState: root.syncDisplayState
            storageState: root.storageDisplayState
            lezReady: root.encodedStatusValue(root.lezState, "ready") === "1"
            palaceState: root.encodedStatusValue(root.palaceState, "palace")
                || "closed"
            localDevelopment: root.localDevelopmentMode
            doorPhase: root.doorVmPhase || "idle"
        }

        AdminDrawer {
            app: root
            visible: root.adminDrawerOpen && root.roomUsable
            enabled: visible
            onClosed: root.adminDrawerOpen = false
        }

    }

    AssetAuthoringPanel {
        id: backgroundModerationPanel
        visible: root.backgroundModerationOpen
        enabled: visible
        app: root
    }

    OnboardingOverlay {
        app: root
        visible: !root.roomUsable
            && !(root.onboardingPhase === "authoring-rooms"
                 && root.backgroundModerationOpen)
        enabled: visible
    }
}
