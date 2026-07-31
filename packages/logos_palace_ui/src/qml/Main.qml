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
        ? invocationError : (backend ? backend.lastActionReceipt : "")

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

    function propDraftReady() {
        return validAssetIdentifier(propDraftId)
            && validAssetIdentifier(propDraftLayer)
            && parsedAssetAnchor(propDraftAnchorX) >= 0
            && parsedAssetAnchor(propDraftAnchorY) >= 0
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
        return JSON.stringify({
            "schema": "logos.palace.asset-authoring-render",
            "version": 1,
            "open": backgroundModerationOpen,
            "cardCount": backgroundGrid.count,
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
        var usableWidth = Math.max(1, roomCanvas.width - 180)
        return 90 + clampCoordinate(coordinate) * usableWidth / 10000
    }

    function roomY(coordinate) {
        var usableHeight = Math.max(1, roomCanvas.height - 330)
        return 110 + clampCoordinate(coordinate) * usableHeight / 10000
    }

    function rejectedNotReady() {
        invocationError = "rejected=ui-not-ready"
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
            } else {
                invocationError = ""
                if (onAccepted)
                    onAccepted(receipt)
            }
            ++invocationSequence
        }, function (error) {
            invocationError = "rejected=ui-remote-call;" + String(error)
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
        return watchAction(
            backend.publishVerifiedPng(String(handle)), null)
    }

    function gate3PublicationStatus(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.publicationStatus(String(handle)), null)
    }

    function reviewAsset(handle, decision) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.reviewAsset(
                String(handle), String(decision)),
            null)
    }

    function publishAsset(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.publishAsset(String(handle)),
            null)
    }

    function assignRoomBackground(roomId, handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.assignRoomBackground(
                String(roomId), String(handle)),
            null)
    }

    function assignPropAsset(propId, handle, anchorX, anchorY, layer) {
        if (!ready || !backend)
            return rejectedNotReady()
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

    FrameAnimation {
        running: root.backgroundScreenshotFenceRunning
        onTriggered: {
            root.backgroundScreenshotFenceFrame = currentFrame
            root.backgroundScreenshotFenceRunning = false
            ++root.backgroundPreviewEpoch
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "#15110d"

        Rectangle {
            id: roomCanvas
            objectName: "palaceRoomCanvas"
            anchors.fill: parent
            anchors.margins: 20
            color: "#312a24"
            radius: 10
            border.color: "#d5b77a"
            border.width: 3
            clip: true

            Image {
                id: roomBackground
                objectName: "palaceRoomBackground"
                anchors.fill: parent
                source: root.roomBackgroundHandle.length === 64
                    ? "image://basecamp-verified/" + root.roomBackgroundHandle
                    : ""
                fillMode: Image.PreserveAspectCrop
                smooth: false
            }

            Rectangle {
                objectName: "palaceRoomBackgroundPlaceholder"
                anchors.fill: parent
                color: "#312a24"
                visible: roomBackground.status !== Image.Ready

                Text {
                    anchors.centerIn: parent
                    text: "Verified room art unavailable"
                    color: "#f3c36b"
                    font.pixelSize: 16
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#1b130d"
                opacity: roomBackground.status === Image.Ready ? 0.18 : 0
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 20
                text: root.roomTitle
                font.pixelSize: 30
                font.bold: true
                color: "#fff2cf"
                z: 8
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 58
                text: root.participants.length + " participant"
                    + (root.participants.length === 1 ? "" : "s")
                color: "#e5d2aa"
                font.pixelSize: 12
                z: 8
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
                            width: Math.min(190, Math.max(
                                88, remoteSpeechText.implicitWidth + 24))
                            height: remoteSpeechText.implicitHeight + 16
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
                                width: Math.min(164, implicitWidth)
                                text: participantDelegate.participantSpeech
                                color: "#2b2016"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        Rectangle {
                            id: remoteAvatar
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            width: 66
                            height: 66
                            radius: width / 2
                            color: "#68a3a0"
                            border.color: "#d9ffef"
                            border.width: 3

                            Text {
                                anchors.centerIn: parent
                                text: participantDelegate.participantDisplayName.length > 0
                                    ? participantDelegate.participantDisplayName
                                          .charAt(0).toUpperCase()
                                    : "?"
                                color: "#102322"
                                font.pixelSize: 24
                                font.bold: true
                            }
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

            Button {
                objectName: "palaceRoomDoor"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: actionDock.top
                anchors.bottomMargin: 12
                text: root.roomTitle !== "Atrium"
                    ? "Door to Atrium"
                    : (root.gate5DoorBlocked
                       ? "Door finalizing…" : "Door to Lounge")
                enabled: root.ready
                    && (root.roomTitle !== "Atrium"
                        || !root.gate5DoorBlocked)
                z: 10
                onClicked: {
                    if (root.roomTitle === "Atrium") {
                        root.gate5UseDoor()
                    } else {
                        root.watchAction(
                            root.backend.enterRoom("atrium"), null)
                    }
                }
            }

            Rectangle {
                id: actionDock
                objectName: "palaceActionDock"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                width: Math.max(320, Math.min(parent.width - 32, 820))
                height: 112
                radius: 14
                color: "#211a14ed"
                border.color: "#846b45"
                z: 10

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 160
                        spacing: 5

                        Text {
                            text: "Talk in the room"
                            color: "#fff2cf"
                            font.bold: true
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            TextField {
                                id: chatInput
                                objectName: "palaceChatInput"
                                Layout.fillWidth: true
                                placeholderText: "Say something"
                                maximumLength: 280
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
                                text: "Send"
                                enabled: root.ready && chatInput.text.length > 0
                                onClicked: {
                                    root.gate2Say(chatInput.text)
                                    chatInput.clear()
                                }
                            }
                        }

                        Text {
                            objectName: "palaceLastActionReceipt"
                            Layout.fillWidth: true
                            text: root.lastActionReceipt.length > 0
                                ? root.lastActionReceipt : "No action yet"
                            color: root.lastActionReceipt.indexOf("rejected=") === 0
                                   || root.lastActionReceipt.indexOf(
                                       "degraded;reason=") === 0
                                ? "#ff9c8f" : "#b9dcae"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    GridLayout {
                        columns: 3
                        rowSpacing: 2
                        columnSpacing: 2

                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                        Button {
                            objectName: "palaceMoveUp"
                            text: "↑"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX, root.localMotionY - 750)
                        }
                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                        Button {
                            objectName: "palaceMoveLeft"
                            text: "←"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX - 750, root.localMotionY)
                        }
                        Rectangle {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            radius: 6
                            color: "#d5b77a"
                        }
                        Button {
                            objectName: "palaceMoveRight"
                            text: "→"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX + 750, root.localMotionY)
                        }
                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                        Button {
                            objectName: "palaceMoveDown"
                            text: "↓"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX, root.localMotionY + 750)
                        }
                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                    }

                    ColumnLayout {
                        spacing: 5

                        Button {
                            objectName: "palaceWearAssignedProp"
                            text: "Wear assigned prop"
                            visible: root.availablePropId.length > 0
                            enabled: root.ready
                                && root.localWornPropId
                                    !== root.availablePropId
                            onClicked: root.gate2Wear(
                                root.availablePropId)
                        }
                        Button {
                            objectName: "palaceRemoveAssignedProp"
                            text: "Remove assigned prop"
                            visible: root.availablePropId.length > 0
                            enabled: root.ready
                                && root.localWornPropId
                                    === root.availablePropId
                            onClicked: root.gate2Remove(
                                root.availablePropId)
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 34
            width: 132
            height: 76
            color: "#211a14dd"
            radius: 8
            z: 20

            Column {
                anchors.centerIn: parent
                spacing: 5
                Text {
                    text: "Rooms"
                    color: "#fff2cf"
                    font.bold: true
                }
                Text {
                    text: "Atrium\nLounge"
                    color: "#f0dfba"
                    font.pixelSize: 12
                }
            }
        }

        Rectangle {
            objectName: "palaceModerationPanel"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 34
            anchors.topMargin: 190
            width: 230
            height: 250
            radius: 8
            color: "#211a14ee"
            border.color: "#846b45"
            z: 20

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5

                Text {
                    text: "Moderation"
                    color: "#fff2cf"
                    font.bold: true
                    font.pixelSize: 13
                }

                Repeater {
                    model: root.participants.length

                    delegate: Row {
                        required property int index
                        property var participant:
                            root.participants[index] || ({})
                        property string participantName:
                            String(participant.displayName
                                   || participant.userId || "Unknown")
                        property string participantUserId:
                            String(participant.userId || "")
                        width: 210
                        height: 28
                        spacing: 6

                        Text {
                            width: 126
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.participantName
                            elide: Text.ElideRight
                            color: "#e5d2aa"
                            font.pixelSize: 11
                        }

                        Button {
                            objectName: "palaceBanUserButton"
                            property string subjectUserId:
                                parent.participantUserId
                            width: 72
                            height: 26
                            text: "Ban user"
                            enabled: root.ready
                                && subjectUserId.length === 64
                            onClicked: root.gate4BanUser(subjectUserId)
                        }
                    }
                }

                Button {
                    objectName: "palaceBanAssignedPropButton"
                    width: 148
                    height: 28
                    text: "Ban assigned prop"
                    visible: root.availablePropId.length > 0
                    enabled: root.ready
                    onClicked: root.gate4BanProp(
                        root.availablePropId)
                }

                Button {
                    objectName: "palaceBackgroundModerationButton"
                    width: 154
                    height: 28
                    text: "Palace assets"
                    enabled: root.ready
                    onClicked: root.backgroundModerationOpen = true
                }

                Text {
                    objectName: "palaceModerationStatus"
                    width: 210
                    text: "Status: "
                        + (root.encodedStatusValue(
                            root.moderationState, "state") || "idle")
                        + (root.encodedStatusValue(
                            root.moderationState, "action").length > 0
                           ? " · action "
                             + root.encodedStatusValue(
                                 root.moderationState, "action")
                           : "")
                    color: root.encodedStatusValue(
                        root.moderationState, "state") === "finalized"
                        ? "#a7e3a0"
                        : (root.encodedStatusValue(
                               root.moderationState, "state") === "rejected"
                           ? "#ff9c8f" : "#f3c36b")
                    elide: Text.ElideRight
                    font.pixelSize: 10
                }
            }
        }

        Rectangle {
            id: backgroundModeration
            objectName: "palaceBackgroundModeration"
            anchors.centerIn: parent
            width: Math.min(parent.width - 80, 930)
            height: Math.min(parent.height - 80, 620)
            radius: 12
            color: "#f518130f"
            border.color: "#d5b77a"
            border.width: 2
            visible: root.backgroundModerationOpen
            z: 50

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
                                + " · approve, upload, then assign"
                            color: "#c9b78e"
                            font.pixelSize: 11
                        }
                    }

                    Button {
                        objectName: "palaceAssetSelectFile"
                        text: root.assetImportRunning
                            ? "Importing…" : "Add PNG…"
                        enabled: root.ready && !root.assetImportRunning
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
                        text: root.propDraftId
                        onTextEdited: root.propDraftId = text
                    }

                    TextField {
                        objectName: "palaceAssetPropAnchorX"
                        Layout.preferredWidth: 76
                        placeholderText: "anchor X"
                        inputMethodHints: Qt.ImhDigitsOnly
                        text: root.propDraftAnchorX
                        onTextEdited: root.propDraftAnchorX = text
                    }

                    TextField {
                        objectName: "palaceAssetPropAnchorY"
                        Layout.preferredWidth: 76
                        placeholderText: "anchor Y"
                        inputMethodHints: Qt.ImhDigitsOnly
                        text: root.propDraftAnchorY
                        onTextEdited: root.propDraftAnchorY = text
                    }

                    TextField {
                        objectName: "palaceAssetPropLayer"
                        Layout.preferredWidth: 92
                        placeholderText: "layer"
                        text: root.propDraftLayer
                        onTextEdited: root.propDraftLayer = text
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.propDraftReady()
                            ? "Ready to assign"
                            : "Enter metadata before assigning a prop"
                        color: root.propDraftReady()
                            ? "#a7e3a0" : "#8f826a"
                        elide: Text.ElideRight
                        font.pixelSize: 10
                    }
                }

                GridView {
                    id: backgroundGrid
                    objectName: "palaceBackgroundGrid"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    cellWidth: Math.max(210, Math.floor(width / 4))
                    cellHeight: 252
                    model: root.authoringAssets
                    boundsBehavior: Flickable.StopAtBounds

                    delegate: Rectangle {
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
                        width: backgroundGrid.cellWidth - 10
                        height: backgroundGrid.cellHeight - 10
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
                                source: String(backgroundCard.asset.handle
                                               || "").length === 64
                                    ? "image://basecamp-verified/"
                                      + String(backgroundCard.asset.handle)
                                    : ""
                                fillMode: Image.PreserveAspectCrop
                                smooth: true
                                asynchronous: false
                                onStatusChanged: {
                                    if (status === Image.Ready
                                            && !backgroundCard
                                                .previewCounted) {
                                        backgroundCard.previewCounted = true
                                        ++root.backgroundReadyImageCount
                                    } else if (status !== Image.Ready
                                               && backgroundCard
                                                   .previewCounted) {
                                        backgroundCard.previewCounted = false
                                        --root.backgroundReadyImageCount
                                    }
                                    ++root.backgroundPreviewEpoch
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    Layout.fillWidth: true
                                    text: String(backgroundCard.asset.label
                                                 || backgroundCard.handle)
                                    color: "#fff2cf"
                                    font.bold: true
                                    elide: Text.ElideRight
                                    font.pixelSize: 12
                                }

                                Text {
                                    visible:
                                        backgroundCard.assignedRooms.length > 0
                                        || backgroundCard.assignedProps.length
                                           > 0
                                    text: backgroundCard.assignedRooms
                                        .concat(backgroundCard.assignedProps)
                                        .join(" · ").toUpperCase()
                                    color: "#a7e3a0"
                                    font.bold: true
                                    font.pixelSize: 9
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: Number(backgroundCard.asset.width)
                                    + "×" + Number(backgroundCard.asset.height)
                                    + " · " + String(
                                        backgroundCard.asset.reviewState)
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
                                         + backgroundCard.handle.slice(0, 12)
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
                                    text: backgroundCard.publicationState
                                            === "published"
                                        ? "Uploaded" : "Approve & upload"
                                    enabled: root.ready
                                        && backgroundCard.publicationState
                                            !== "published"
                                        && backgroundCard.handle.length
                                            > 0
                                    onClicked:
                                        root.reviewAndPublishAsset(
                                            backgroundCard.handle)
                                }

                                Button {
                                    objectName:
                                        "palaceAssetReject-"
                                        + backgroundCard.handle
                                    text: "Reject"
                                    enabled: root.ready
                                        && backgroundCard.publicationState
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
                                    text: "Set Atrium"
                                    enabled: root.ready
                                        && backgroundCard.publicationState
                                            === "published"
                                        && backgroundCard.handle.length === 64
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
                                    text: "Set prop"
                                    enabled: root.ready
                                        && backgroundCard.publicationState
                                            === "published"
                                        && backgroundCard.handle.length === 64
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
                                    text: "Set Lounge"
                                    enabled: root.ready
                                        && backgroundCard.publicationState
                                            === "published"
                                        && backgroundCard.handle.length === 64
                                    onClicked:
                                        root.assignRoomBackground(
                                            "lounge",
                                            backgroundCard.handle)
                                }
                            }
                        }
                        Component.onDestruction: {
                            if (previewCounted)
                                --root.backgroundReadyImageCount
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    text: root.lastActionReceipt.length > 0
                        ? root.lastActionReceipt
                        : "Draft assignment becomes authority when Palace creation finalizes."
                    color: root.lastActionReceipt.indexOf("rejected=") === 0
                        ? "#ff9c8f" : "#a7e3a0"
                    elide: Text.ElideRight
                    font.pixelSize: 10
                }
            }
        }

        Rectangle {
            objectName: "palaceDeliveryStatus"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 34
            width: 196
            height: 140
            radius: 8
            color: "#211a14dd"
            z: 20

            Column {
                anchors.centerIn: parent
                spacing: 3

                Text {
                    id: deliveryStatusText
                    text: root.ready
                        ? "Delivery: " + (root.statusValue("state") || "offline")
                        : "Connecting"
                    color: root.statusValue("state") === "online"
                        ? "#a7e3a0" : "#f3c36b"
                    font.bold: true
                    font.pixelSize: 12
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.connectedPeerCount + " connected peer"
                        + (root.connectedPeerCount === 1 ? "" : "s")
                    color: "#e5d2aa"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "LEZ: "
                        + (root.encodedStatusValue(
                            root.lezState, "ready") === "1"
                           ? "synchronized" : "offline")
                    color: root.encodedStatusValue(
                        root.lezState, "ready") === "1"
                        ? "#a7e3a0" : "#f3c36b"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Palace: "
                        + (root.encodedStatusValue(
                            root.palaceState, "palace") || "closed")
                    color: root.encodedStatusValue(
                        root.palaceState, "palace") === "open"
                        ? "#a7e3a0" : "#f3c36b"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Assets: "
                        + (root.lastActionReceipt.indexOf(
                               "degraded;reason=") === 0
                           ? "degraded"
                           : (root.encodedStatusValue(
                                  root.storageStatus, "state")
                              || root.encodedStatusValue(
                                  root.storageStatus, "storage")
                              || "offline"))
                    color: root.lastActionReceipt.indexOf(
                               "degraded;reason=") === 0
                           || root.storageStatus.indexOf("degraded") !== -1
                        ? "#ff9c8f" : "#e5d2aa"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Projection: " + root.syncHealth
                    color: root.syncHealth === "fully_synchronized"
                        ? "#a7e3a0" : "#f3c36b"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Door: " + (root.gate5VmPhase || "idle")
                    color: root.gate5VmPhase === "degraded"
                        ? "#ff9c8f"
                        : (root.gate5VmPhase === "promoted"
                           ? "#a7e3a0" : "#e5d2aa")
                    font.pixelSize: 10
                }
            }
        }
    }
}
