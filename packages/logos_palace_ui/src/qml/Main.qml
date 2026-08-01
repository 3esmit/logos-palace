pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "palaceGate2Root"

    readonly property var backend: logos.module("logos_palace_ui")
    readonly property string roomTitle: backend ? backend.roomTitle : "Connecting..."
    readonly property string roomBackgroundHandle: backend ? backend.roomBackgroundHandle : ""
    readonly property string syncHealth: backend ? backend.syncHealth : "recovering"
    readonly property string deliveryStatus: backend
        ? backend.deliverySessionStatus : "state=unavailable"
    readonly property string storageStatus: backend
        ? backend.storageStatus : "storage=unavailable"
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
    readonly property string spotState: backend
        ? backend.spotState
        : "vm=idle;action=;navigation=0;reason=not-started"
    readonly property string spotActionId: backend
        ? backend.spotActionId : ""
    readonly property string spotReceipt: backend
        ? backend.spotReceipt : ""
    readonly property string participantProjection: backend
        ? backend.participantProjection : "[]"
    readonly property string nodeEvidence: backend
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

    // Stable inspector contract used by the compiled Gate 2 harness.
    readonly property string gate2Status: deliveryStatus
    readonly property string gate2Projection: participantProjection
    readonly property string gate2NodeEvidence: nodeEvidence
    readonly property string gate2Receipt: lastActionReceipt
    readonly property int gate2ParticipantCount: participants.length
    readonly property bool gate2Ready: ready

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
    // Human moderation is an admin-only LEZ command. Never infer it from
    // display identity or room state; Core derives it from finalized authority.
    readonly property bool canBanUser: ready
        && encodedStatusValue(moderationCapabilityState, "authority")
            === "finalized"
        && encodedStatusValue(moderationCapabilityState, "can_ban_user")
            === "1"
    readonly property bool canBanProp: ready
        && encodedStatusValue(moderationCapabilityState, "authority")
            === "finalized"
        && encodedStatusValue(moderationCapabilityState, "can_ban_prop")
            === "1"
    readonly property int connectedPeerCount: parseConnectedPeerCount(nodeEvidence)
    property bool ready: false
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
        if (ready && !backgroundModerationOpen && !propBagOpen
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

    function watchAction(pendingCall, onAccepted) {
        logos.watch(pendingCall, function (value) {
            var receipt = String(value)
            // Surface terminal rejections through the local sequence so
            // moderation waits can fail closed. Success clears any prior
            // local rejection and may run the optional accepted callback.
            if (receipt.indexOf("rejected=") === 0) {
                invocationError = receipt
                watchedActionReceipt = ""
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

    function gate2Start(configJson) {
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

    function gate2Say(text) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.say(String(text)), null)
    }

    function gate2Move(x, y) {
        if (!ready || !backend)
            return rejectedNotReady()
        var nextX = Math.round(Number(x))
        var nextY = Math.round(Number(y))
        return watchAction(backend.moveAvatar(nextX, nextY), function () {
            localMotionX = clampCoordinate(nextX)
            localMotionY = clampCoordinate(nextY)
        })
    }

    function gate2Wear(propId) {
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

    function gate2Remove(propId) {
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

    function gate2RefreshPresence() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshPresence(), null)
    }

    function gate3StartStorage(configJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startStorage(String(configJson)), null)
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
        if (encodedStatusValue(
                assetAuthoringCapabilityState, "authority")
                === "finalized") {
            return "Read-only. Only Palace owner can approve, upload, or assign assets."
        }
        return "Read-only. Only current draft creator can change assets."
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

    onParticipantsChanged: syncSelectedModerationUser()

    Shortcut {
        sequence: "Esc"
        enabled: root.backgroundModerationOpen || root.propBagOpen
            || root.roomListOpen || root.userListOpen
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
                        onClicked: root.gate2Move(
                            root.localMotionX, root.localMotionY - 750)
                    }
                    Button {
                        objectName: "palaceMoveLeft"
                        width: 28
                        height: 28
                        text: "←"
                        Accessible.name: "Move left"
                        enabled: root.ready
                        onClicked: root.gate2Move(
                            root.localMotionX - 750, root.localMotionY)
                    }
                    Button {
                        objectName: "palaceMoveRight"
                        width: 28
                        height: 28
                        text: "→"
                        Accessible.name: "Move right"
                        enabled: root.ready
                        onClicked: root.gate2Move(
                            root.localMotionX + 750, root.localMotionY)
                    }
                    Button {
                        objectName: "palaceMoveDown"
                        width: 28
                        height: 28
                        text: "↓"
                        Accessible.name: "Move down"
                        enabled: root.ready
                        onClicked: root.gate2Move(
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
                        onClicked: root.gate2Wear(root.availablePropId)
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
                        onClicked: root.gate2Remove(root.availablePropId)
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

                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.roomTitle
                    color: "#000080"
                    font.bold: true
                    font.pixelSize: 12
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
                    root.gate2Move(coordinate.x, coordinate.y)
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
                                root.gate2Remove(root.availablePropId)
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
                                root.gate2Say(text)
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
                            root.gate2Say(chatInput.text)
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
                        onClicked: root.gate2Wear(root.availablePropId)
                    }
                    Button {
                        text: "Remove"
                        width: 72
                        height: 26
                        enabled: root.ready
                            && root.availablePropId.length > 0
                            && root.localWornPropId
                                === root.availablePropId
                        onClicked: root.gate2Remove(root.availablePropId)
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
                272, 154
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
                    + " · LEZ "
                    + (root.encodedStatusValue(root.lezState, "ready") === "1"
                       ? "ok" : "off")
                    + " · Palace "
                    + (root.encodedStatusValue(root.palaceState, "palace")
                       || "closed")
                    + " · door " + (root.gate5VmPhase || "idle")
            }
        }
    }
}
