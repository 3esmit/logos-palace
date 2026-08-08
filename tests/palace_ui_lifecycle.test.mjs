import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { test } from "node:test";
import {
  applyPreviewDestroyed,
  applyPreviewReadyTransition,
  canvasPixelsToProtocol,
  clampNonNegative,
  idleImportState,
  resetAuthoringPreviewCount,
  shouldCancelOrphanStage,
} from "./palace_ui_lifecycle.mjs";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const mainQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/Main.qml"),
  "utf8",
);
const roomViewQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/room/RoomView.qml"),
  "utf8",
);
const participantViewQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/room/ParticipantView.qml"),
  "utf8",
);
const roomToolbarQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/room/RoomToolbar.qml"),
  "utf8",
);
const roomUtilityQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/room/RoomUtilityPanels.qml"),
  "utf8",
);
const assetAuthoringPanelQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/admin/AssetAuthoringPanel.qml"),
  "utf8",
);
const connectionStatusQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/components/ConnectionStatus.qml"),
  "utf8",
);
const chatBarQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/room/ChatBar.qml"),
  "utf8",
);
const onboardingDetailsQml = readFileSync(
  join(root, "packages/logos_palace_ui/src/qml/onboarding/OnboardingDetails.qml"),
  "utf8",
);
const backendCpp = readFileSync(
  join(root, "packages/logos_palace_ui/src/logos_palace_ui_backend.cpp"),
  "utf8",
);
const backendH = readFileSync(
  join(root, "packages/logos_palace_ui/src/logos_palace_ui_backend.h"),
  "utf8",
);
const coreImpl = readFileSync(
  join(root, "packages/palace_core/src/palace_core_impl.cpp"),
  "utf8",
);
const uiRep = readFileSync(
  join(root, "packages/logos_palace_ui/src/logos_palace_ui.rep"),
  "utf8",
);
const uiMetadata = JSON.parse(readFileSync(
  join(root, "packages/logos_palace_ui/metadata.json"),
  "utf8",
));

test("preview ready transitions never go negative and balance destroy", () => {
  let count = 0;
  let a = false;
  let b = false;
  ({ nextCount: count, nextCounted: a } = applyPreviewReadyTransition(
    count,
    a,
    true,
  ));
  ({ nextCount: count, nextCounted: b } = applyPreviewReadyTransition(
    count,
    b,
    true,
  ));
  assert.equal(count, 2);
  ({ nextCount: count, nextCounted: a } = applyPreviewDestroyed(count, a));
  assert.equal(count, 1);
  assert.equal(a, false);
  ({ nextCount: count, nextCounted: b } = applyPreviewDestroyed(count, b));
  assert.equal(count, 0);
  // Double-destroy is a no-op
  ({ nextCount: count, nextCounted: b } = applyPreviewDestroyed(count, b));
  assert.equal(count, 0);
  assert.equal(clampNonNegative(-3), 0);
});

test("authoring close resets preview count to zero", () => {
  assert.equal(resetAuthoringPreviewCount(), 0);
});

test("import abandon yields idle flags and bumps generation", () => {
  const idle = idleImportState(4);
  assert.equal(idle.running, false);
  assert.equal(idle.phase, "idle");
  assert.equal(idle.session, "");
  assert.equal(idle.generation, 5);
  assert.equal(idle.trace, null);
});

test("orphan stage sessions are cancelled only when unmatched", () => {
  assert.equal(
    shouldCancelOrphanStage("0123456789abcdef0123456789abcdef", false),
    true,
  );
  assert.equal(
    shouldCancelOrphanStage("0123456789abcdef0123456789abcdef", true),
    false,
  );
  assert.equal(shouldCancelOrphanStage("not-a-session", false), false);
});

test("canvas clicks map to bounded Palace protocol coordinates", () => {
  assert.deepEqual(canvasPixelsToProtocol(100, 104, 1000, 800), {
    x: 0,
    y: 0,
  });
  assert.deepEqual(canvasPixelsToProtocol(900, 634, 1000, 800), {
    x: 10000,
    y: 10000,
  });
  assert.deepEqual(canvasPixelsToProtocol(-100, 10000, 1000, 800), {
    x: 0,
    y: 10000,
  });
  assert.deepEqual(canvasPixelsToProtocol(Number.NaN, Number.NaN, 1000, 800), {
    x: 0,
    y: 0,
  });
});

test("Main.qml wires destroy/reset cleanup on real paths", () => {
  assert.match(mainQml, /function noteAuthoringPreviewReady/);
  assert.match(mainQml, /function noteAuthoringPreviewLost/);
  assert.match(mainQml, /function resetAuthoringPreviewState/);
  assert.match(mainQml, /function syncSelectedModerationUser\(\)/);
  assert.match(mainQml, /Component\.onDestruction:\s*root\.abandonAssetImport\(\)/);
  assert.match(mainQml, /resetAuthoringPreviewState\(\)/);
  assert.match(mainQml, /visible:\s*root\.backgroundModerationOpen/);
  assert.match(assetAuthoringPanelQml, /onVisibleChanged:\s*\{[\s\S]{0,120}app\.resetAuthoringPreviewState\(\)/);
  assert.match(roomToolbarQml, /objectName:\s*"palaceToolbox"/);
  assert.match(mainQml, /objectName:\s*"palaceStatusStrip"/);
  assert.match(chatBarQml, /objectName:\s*"palaceInputStrip"/);
  assert.match(uiRep, /SLOT\(QString beginAssetImport\(QString label, qint64 byteLength\)\)/);
  assert.match(uiRep, /SLOT\(QString appendAssetImportChunk\(QString base64Chunk\)\)/);
  assert.match(uiRep, /PROP\(QString assetImportStatus="state=idle" READONLY\)/);
  assert.match(backendCpp, /QString LogosPalaceUiBackend::beginAssetImport\(/);
  assert.match(backendCpp, /m_assetImportExpectedSequence/);
  assert.doesNotMatch(
    mainQml,
    /backend\.(beginAssetStage|appendAssetStageChunk|commitAssetStage|cancelAssetStage)\(/,
  );
  assert.match(mainQml, /objectName:\s*"palacePropTrash"/);
  assert.match(mainQml, /objectName:\s*"palacePropBag"/);
  assert.match(roomUtilityQml, /objectName:\s*"palacePropBagPanel"/);
  assert.match(roomToolbarQml, /objectName:\s*"palaceUserListToggle"/);
  assert.match(mainQml, /Users:\s*"\s*\+\s*root\.participants\.length/);
});

test("Palace selects verified assets from its active persistence profile", () => {
  assert.deepEqual(uiMetadata.verified_asset_producers, ["palace_core"]);
  assert.deepEqual(uiMetadata.verified_asset_profile, {
    directory: "palace-profiles",
    environment: "PALACE_LEZ_PROFILE",
    default: "release",
    allowed_profiles: ["release", "local-development"],
  });
  assert.match(
    roomViewQml,
    /objectName:\s*"palaceRoomBackground"[\s\S]{0,420}image:\/\/basecamp-verified\//,
  );
  assert.match(
    roomViewQml,
    /objectName:\s*"palaceRoomBackgroundPlaceholder"[\s\S]{0,800}roomBackground\.status !== Image\.Ready/,
  );
});

test("local development keeps its unavailable public finality visible", () => {
  assert.match(
    mainQml,
    /readonly property bool localDevelopmentMode:[\s\S]{0,180}locallyCommittedAuthority[\s\S]{0,180}localDevelopmentProfile/,
  );
  assert.match(
    roomToolbarQml,
    /objectName:\s*"palaceLocalDevelopmentIndicator"[\s\S]{0,180}visible:\s*toolbar\.app\.localDevelopmentMode/,
  );
  assert.match(roomToolbarQml, /Local development · public finality unavailable/);
});

test("room chrome exposes synchronization and Storage degradation", () => {
  assert.match(mainQml, /readonly property string syncDisplayState/);
  assert.match(mainQml, /syncHealth === "fully_synchronized" \? "ok" : syncHealth/);
  assert.match(mainQml, /readonly property string storageDisplayState/);
  assert.match(mainQml, /encodedStatusValue\(storageStatus, "catalog"\)/);
  assert.match(connectionStatusQml, /syncState/);
  assert.match(connectionStatusQml, /storageState/);
});

test("creator onboarding retries only sequencer visibility races", () => {
  assert.match(backendCpp, /bool isRetryableDurableActionReceipt/);
  assert.match(backendCpp, /rejected=lez-stable-account-read;reason=/);
  assert.match(backendCpp, /rejected=lez-observation;reason=/);
  assert.match(backendCpp, /transaction-not-materialized/);
  assert.match(backendCpp, /const auto retryOrFail =/);
  assert.match(
    backendCpp,
    /QStringLiteral\("state="\) \+ m_onboardingWorkflowPhase/,
  );
  assert.doesNotMatch(mainQml, /function retryableCreatorActionRejection\(receipt\)/);
  assert.match(
    mainQml,
    /Waiting for LEZ to expose the new Palace…/,
  );
});

test("creator onboarding materializes the entry-room state before enabling its door", () => {
  assert.match(uiRep, /SLOT\(QString beginPalaceCreation\(QString title\)\)/);
  assert.match(uiRep, /PROP\(QString onboardingWorkflowStatus=\"state=idle\" READONLY\)/);
  const onboardingWorkflowCpp = backendCpp.slice(
    backendCpp.indexOf("void LogosPalaceUiBackend::driveOnboardingWorkflow()"),
  );
  assert.match(
    onboardingWorkflowCpp,
    /void LogosPalaceUiBackend::driveOnboardingWorkflow\(\)[\s\S]{0,20000}modules\(\)\.palace_core[\s\S]{0,80}createInitialRoomState\(\)/,
  );
  const initialRoomStateStart = coreImpl.indexOf(
    "std::string PalaceCoreImpl::createInitialRoomState()",
  );
  const initialRoomStateEnd = coreImpl.indexOf(
    "std::string PalaceCoreImpl::openPalace(",
    initialRoomStateStart,
  );
  assert.ok(initialRoomStateStart >= 0 && initialRoomStateEnd > initialRoomStateStart);
  const initialRoomState = coreImpl.slice(
    initialRoomStateStart,
    initialRoomStateEnd,
  );
  assert.match(initialRoomState, /buildPalaceInitialRoomStateV1/);
  assert.match(initialRoomState, /submitPalaceInstruction/);
  assert.doesNotMatch(mainQml, /function prepareInitialRoomState\(\)/);
  assert.doesNotMatch(mainQml, /function createInitialRoomState\(\)/);
  assert.doesNotMatch(mainQml, /backend\.createInitialRoomState\(\)/);
  assert.doesNotMatch(mainQml, /onboardingCreatedPalaceRetryTimer/);
  assert.match(mainQml, /onOnboardingWorkflowStatusChanged/);
  assert.match(mainQml, /confirming-initial-room-state/);
  assert.match(
    mainQml,
    /readonly property bool roomUsable:[\s\S]{0,180}entryRoomStateReady[\s\S]{0,180}onboardingInitialRoomStatePending/,
  );
});

test("creator onboarding can resume room setup from existing LEZ identity", () => {
  assert.match(
    mainQml,
    /readonly property bool onboardingResumeReady:[\s\S]{0,220}onboardingLezReady[\s\S]{0,220}onboardingIdentityReady/,
  );
  assert.match(
    mainQml,
    /function onboardingInputReady\(\)\s*{[\s\S]{0,120}if \(onboardingResumeReady\)\s*return true/,
  );
  assert.match(
    mainQml,
    /function startOnboarding\(\)\s*{[\s\S]{0,1200}if \(onboardingResumeReady\)[\s\S]{0,400}enterCreatorMode\(\)/,
  );
  const onboardingStart = mainQml.indexOf("function startOnboarding()");
  const onboardingEnd = mainQml.indexOf("function publishRoomSetup()", onboardingStart);
  assert.ok(onboardingStart >= 0 && onboardingEnd > onboardingStart);
  const onboardingFlow = mainQml.slice(onboardingStart, onboardingEnd);
  assert.match(onboardingFlow, /backend\.preparePalaceOnboarding\(/);
  assert.doesNotMatch(onboardingFlow, /backend\.(startLez|createIdentity)\(/);
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::preparePalaceOnboarding\([\s\S]{0,1600}modules\(\)\.palace_core\.startLez\(/,
  );
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::preparePalaceOnboarding\([\s\S]{0,1600}modules\(\)\.palace_core\.createIdentity\(/,
  );
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::preparePalaceOnboarding\([\s\S]{0,2200}resumeExistingPalace\(/,
  );
  assert.match(
    uiRep,
    /SLOT\(QString preparePalaceOnboarding\(QString mode, QString password, QString displayName, QString palaceAddress, QString catalogBase64, QString peerId, QString addressesJson\)\)/,
  );
  assert.match(mainQml, /backend\.beginPalaceCreation\(title\)/);
  assert.match(mainQml, /backend\.resumePalaceOnboarding\(\)/);
  assert.match(mainQml, /backend\.resumeExistingPalace\(/);
  assert.doesNotMatch(mainQml, /backend\.(trackDurableAction|trackStorageBundle|trackPalaceRegistration)\(/);
  assert.doesNotMatch(mainQml, /onboarding(Finality|Bundle|JoinRegistration)PollTimer/);
  assert.doesNotMatch(
    mainQml,
    /backend\.(observePalaceTransition|reconcilePalaceTransition)\(/,
  );
  assert.match(
    backendCpp,
    /void LogosPalaceUiBackend::driveOnboardingWorkflow\(\)[\s\S]{0,12000}reconcilePalaceTransition/,
  );
  assert.match(
    backendCpp,
    /void LogosPalaceUiBackend::driveOnboardingWorkflow\(\)[\s\S]{0,2200}mvpStorageBundleStatus\(\)/,
  );
  assert.match(uiRep, /SLOT\(QString resumeExistingPalace\(/);
  assert.match(backendCpp, /QString LogosPalaceUiBackend::resumeExistingPalace\(/);
  assert.match(mainQml, /Continue room setup/);
});

test("joining an existing Palace imports its user-shared Storage catalog before opening", () => {
  assert.match(mainQml, /property string onboardingInvitation:\s*""/);
  assert.match(mainQml, /function applyOnboardingInvitation\(\)/);
  assert.match(
    assetAuthoringPanelQml,
    /objectName:\s*"palaceCopyInvitation"[\s\S]{0,260}sharedInvitationField\.copy\(\)/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /objectName:\s*"palaceSharedInvitation"[\s\S]{0,300}app\.sharedPalaceInvitation/,
  );
  assert.match(
    mainQml,
    /property string onboardingStorageCatalog:\s*""/,
  );
  assert.match(
    mainQml,
    /property string onboardingStoragePeerEndpoint:\s*""/,
  );
  assert.match(
    mainQml,
    /readonly property bool onboardingResumeReady:[\s\S]{0,420}onboardingStorageCatalog\.trim\(\)\.length > 0/,
  );
  assert.match(
    onboardingDetailsQml,
    /objectName:\s*"palaceOnboardingStorageCatalog"[\s\S]{0,520}maximumLength:\s*16384/,
  );
  assert.match(
    onboardingDetailsQml,
    /objectName:\s*"palaceOnboardingStoragePeerEndpoint"[\s\S]{0,520}maximumLength:\s*16384/,
  );
  assert.match(
    mainQml,
    /readonly property string sharedStorageCatalog:[\s\S]{0,160}onboardingBundleStatus/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /objectName:\s*"palaceSharedStorageCatalog"[\s\S]{0,420}readOnly:\s*true/,
  );
  assert.match(
    mainQml,
    /readonly property string sharedStoragePeerEndpoint:[\s\S]{0,100}onboardingStoragePeerEndpoint/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /objectName:\s*"palaceSharedStoragePeerEndpoint"[\s\S]{0,420}readOnly:\s*true/,
  );
  assert.match(mainQml, /function resumeExistingPalaceWorkflow\(\)/);
  assert.match(mainQml, /backend\.resumeExistingPalace\(/);
  assert.doesNotMatch(mainQml, /backend\.prepareExistingPalaceStorage\(/);
  assert.doesNotMatch(mainQml, /function fetchExistingStorageCatalog\(\)/);
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::resumeExistingPalace\([\s\S]{0,900}prepareExistingPalaceStorage\(/,
  );
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::prepareExistingPalaceStorage\(/,
  );
  assert.match(backendCpp, /modules\(\)\.palace_core\.fetchMvpStorageBundle\(/);
  assert.match(
    uiRep,
    /SLOT\(QString prepareExistingPalaceStorage\(QString catalogBase64, QString peerId, QString addressesJson, bool attachPeer\)\)/,
  );
  assert.match(mainQml, /function parseStoragePeerEndpoint\(encoded\)/);
  assert.match(mainQml, /onOnboardingWorkflowStatusChanged/);
  assert.match(mainQml, /fetching-storage-catalog/);
  assert.match(
    onboardingDetailsQml,
    /Shared room catalog \(required for an existing Palace\)/,
  );
});

test("Storage attachment stays explicit and reports a stopped Control node", () => {
  assert.match(uiRep, /SLOT\(QString connectStorage\(\)\)/);
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::connectStorage\(\)[\s\S]{0,280}modules\(\)\.palace_core\.connectStorage\(\)/,
  );
  assert.match(
    mainQml,
    /function connectStorage\(\)[\s\S]{0,520}backend\.connectStorage\(\)/,
  );
  assert.match(assetAuthoringPanelQml, /objectName:\s*"palaceConnectStorage"/);
  assert.match(
    assetAuthoringPanelQml,
    /Start Storage in Logos Control first\. Palace only connects to an already running node\./,
  );
  assert.match(
    coreImpl,
    /std::string PalaceCoreImpl::connectStorage\(\)[\s\S]{0,1600}sessionConfig\.externallyManaged\s*=\s*true/,
  );
  assert.match(
    coreImpl,
    /executeStorageCommands\(attached\.commands\);[\s\S]{0,220}if \(!m_storageSession\.running\(\)\)[\s\S]{0,180}rejected=storage-attach/,
  );
});

test("Main.qml maps canvas clicks below actors and exposes fixed rooms", () => {
  const roomQml = roomViewQml;
  assert.match(
    mainQml,
    /function canvasPixelToProtocol\s*\(\s*pixel,\s*inset,\s*usableSpan\s*\)/,
  );
  assert.match(
    mainQml,
    /function canvasPixelsToProtocol\s*\(\s*pixelX,\s*pixelY\s*\)/,
  );
  assert.match(roomQml, /objectName:\s*"palaceRoomMoveSurface"/);
  assert.match(
    roomQml,
    /objectName:\s*"palaceRoomMoveSurface"[\s\S]{0,220}z:\s*2/,
  );
  assert.match(
    roomQml,
    /var coordinate\s*=\s*roomView\.app\.canvasPixelsToProtocol\(\s*mouse\.x,\s*mouse\.y\s*\)[\s\S]{0,120}roomView\.app\.moveAvatar\(coordinate\.x,\s*coordinate\.y\)/,
  );

  const moveSurfaceOffset = roomQml.indexOf('objectName: "palaceRoomMoveSurface"');
  const participantOffset = roomQml.indexOf('objectName: "palaceParticipants"');
  const doorOffset = roomQml.indexOf('objectName: "palaceRoomDoor"');
  const doorBlock = roomQml.slice(
    doorOffset,
    roomQml.indexOf("onClicked:", doorOffset),
  );
  assert.ok(moveSurfaceOffset >= 0 && moveSurfaceOffset < participantOffset);
  assert.ok(moveSurfaceOffset >= 0 && moveSurfaceOffset < doorOffset);

  assert.match(mainQml, /property bool roomListOpen:\s*false/);
  assert.match(mainQml, /property bool userListOpen:\s*false/);
  assert.match(mainQml, /readonly property int roomCanvasBottomInset:\s*166/);
  assert.match(
    mainQml,
    /roomCanvasVerticalInset:\s*\n\s*roomCanvasTopInset \+ roomCanvasBottomInset/,
  );
  assert.match(roomUtilityQml, /objectName:\s*"palaceRoomListPanel"/);
  assert.match(roomUtilityQml, /objectName:\s*"palaceRoomListAtrium"/);
  assert.match(roomUtilityQml, /objectName:\s*"palaceRoomListLounge"/);
  assert.match(
    roomUtilityQml,
    /objectName:\s*"palaceRoomListAtrium"[\s\S]{0,420}utility\.app\.selectFixedRoom\("atrium"\)/,
  );
  assert.match(
    roomUtilityQml,
    /objectName:\s*"palaceRoomListLounge"[\s\S]{0,520}utility\.app\.selectFixedRoom\("lounge"\)/,
  );
  assert.match(mainQml, /function selectFixedRoom\s*\(\s*roomId\s*\)/);
  assert.match(
    mainQml,
    /if \(selectedRoom === currentRoom\)[\s\S]{0,360}watchedActionReceipt\s*=\s*"ok=room-current;[\s\S]{0,180}return watchedActionReceipt/,
  );
  assert.match(
    mainQml,
    /selectedRoom === "lounge"[\s\S]{0,180}root\.useDoor\(\)/,
  );
  assert.match(
    mainQml,
    /selectedRoom === "atrium"[\s\S]{0,220}backend\.enterRoom\("atrium"\)/,
  );
  assert.match(
    roomToolbarQml,
    /objectName:\s*"palaceToolboxRooms"[\s\S]{0,420}toolbar\.app\.roomListOpen\s*=\s*!toolbar\.app\.roomListOpen/,
  );
  assert.match(
    roomQml,
    /id: roomDoor[\s\S]{0,160}objectName:\s*"palaceRoomDoor"/,
  );
  assert.match(
    doorBlock,
    /anchors\.bottomMargin:\s*roomView\.app\.roomCanvasDoorBottomMargin/,
  );
  assert.match(
    doorBlock,
    /background:\s*Rectangle[\s\S]*contentItem:\s*Text/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /property int cardWidth:\s*Math\.max\(\s*320,/,
  );
  assert.match(assetAuthoringPanelQml, /text: "Set Atrium"/);
  assert.match(assetAuthoringPanelQml, /text: "Set Lounge"/);
});

test("asset authoring capability stays sourced from Core owner status", () => {
  assert.match(
    coreImpl,
    /std::string PalaceCoreImpl::assetAuthoringCapabilityStatus\(\)/,
  );
  assert.match(
    coreImpl,
    /currentAssetAuthoringAuthorityStatus\(false\)/,
  );
  assert.match(
    coreImpl,
    /can_author_assets=/,
  );
  assert.match(
    uiRep,
    /PROP\(QString assetAuthoringCapabilityState="authority=unavailable;can_author_assets=0;reason=core-unavailable" READONLY\)/,
  );
  assert.match(
    backendCpp,
    /setAssetAuthoringCapabilityState\(\s*modules\(\)\.palace_core\.assetAuthoringCapabilityStatus\(\)\);/,
  );
  assert.match(
    mainQml,
    /readonly property bool canManageAssets:[\s\S]{0,220}assetAuthoringCapabilityState,\s*"can_author_assets"\)\s*=== "1"/,
  );
  assert.match(mainQml, /function assetAuthoringReadOnlyMessage\(\)/);
  assert.match(mainQml, /function rejectAssetAuthoringReadOnly\(\)/);
  assert.match(
    mainQml,
    /reviewAsset\(handle,\s*decision\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /assignRoomBackground\(roomId,\s*handle\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /selectAssetFile\(\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /storagePublishBundle\(\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /storagePublishPng\(handle\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /reviewAndPublishAsset\(handle\)[\s\S]{0,260}backend\.approveAndPublishAsset\(String\(handle\)\)/,
  );
  assert.match(
    roomToolbarQml,
    /ToolTip\.text:\s*toolbar\.app\.assetAuthoringReadOnlyMessage\(\)/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /enabled:\s*app\.ready[\s\S]{0,160}app\.canManageAssets[\s\S]{0,160}!app\.assetImportRunning/,
  );
  assert.match(
    backendCpp,
    /setActivePropAsset\([\s\S]{0,240}refreshAssetAuthoringCapabilityState\(\);/,
  );
  const publicationStart = backendCpp.indexOf(
    "QString LogosPalaceUiBackend::approveAndPublishAsset(",
  );
  const publicationEnd = backendCpp.indexOf(
    "QString LogosPalaceUiBackend::assignRoomBackground(",
    publicationStart,
  );
  assert.ok(publicationStart >= 0 && publicationEnd > publicationStart);
  const publicationBlock = backendCpp.slice(
    publicationStart,
    publicationEnd,
  );
  assert.match(publicationBlock, /reviewAsset\(/);
  assert.match(publicationBlock, /modules\(\)\.palace_core\.publishAsset\(/);
  assert.match(uiRep, /SLOT\(QString approveAndPublishAsset\(QString handle\)\)/);
});

test("backend stops poll timers on teardown", () => {
  assert.match(backendH, /class PalaceUiController/);
  assert.match(backendH, /void configure\(RefreshCallback refresh/);
  assert.match(backendH, /~LogosPalaceUiBackend\(\)/);
  assert.match(backendCpp, /stopPollingTimers/);
  assert.match(backendCpp, /m_uiController\.stop\(\)/);
  assert.match(backendCpp, /m_uiController\.configure\(/);
  assert.match(backendCpp, /m_durableActionTracking = false/);
  assert.match(backendCpp, /if \(m_durableActionTracking\)\s+driveDurableAction\(\)/);
});

test("LEZ moderation controls reflect Core materialized capability only", () => {
  const authorityOffset = coreImpl.indexOf(
    "PalaceCoreImpl::humanModerationAuthority(",
  );
  const capabilityOffset = coreImpl.indexOf(
    "std::string PalaceCoreImpl::moderationCapabilityStatus() const",
  );
  const submitOffset = coreImpl.indexOf(
    "std::string PalaceCoreImpl::submitHumanModeration(",
  );
  assert.ok(authorityOffset >= 0 && authorityOffset < capabilityOffset);
  assert.ok(submitOffset >= 0);

  const authoritySource = coreImpl.slice(authorityOffset, capabilityOffset);
  const capabilitySource = coreImpl.slice(capabilityOffset, submitOffset);
  const submitSource = coreImpl.slice(submitOffset);
  const authorityAcceptedOffset = authoritySource.indexOf(
    "authority.accepted = true",
  );
  assert.ok(authorityAcceptedOffset >= 0);
  for (const reason of [
    "authority-account-invalid",
    "root-invalid",
    "root-checkpoint-mismatch",
  ]) {
    const rejectionOffset = authoritySource.indexOf(
      `authority.reason = "${reason}"`,
    );
    const returnOffset = authoritySource.indexOf(
      "return authority;",
      rejectionOffset,
    );
    assert.ok(
      rejectionOffset >= 0 && rejectionOffset < authorityAcceptedOffset,
      `${reason} must reject before authority is accepted`,
    );
    assert.ok(
      returnOffset > rejectionOffset && returnOffset < authorityAcceptedOffset,
      `${reason} must return an unavailable authority`,
    );
  }
  assert.match(capabilitySource, /currentHumanModerationContext\(\)/);
  assert.match(
    capabilitySource,
    /humanModerationAuthority\(\s*context, kModerateUserCapability\s*\)/,
  );
  assert.match(
    capabilitySource,
    /humanModerationAuthority\(\s*context, kModerateAssetCapability\s*\)/,
  );
  assert.match(
    capabilitySource,
    /if \(!user\.accepted \|\| !prop\.accepted\)[\s\S]{0,280}authority=unavailable;can_ban_user=0;can_ban_prop=0/,
  );
  assert.match(capabilitySource, /authoritySnapshotSourceName/);
  assert.ok(
    capabilitySource.indexOf('return "authority="')
      > capabilitySource.indexOf("if (!user.accepted || !prop.accepted)"),
  );
  assert.match(submitSource, /currentHumanModerationContext\(\)/);
  assert.match(
    submitSource,
    /humanModerationAuthority\(\s*context, requiredCapability\)/,
  );

  assert.match(
    uiRep,
    /PROP\(QString moderationCapabilityState="authority=unavailable;can_ban_user=0;can_ban_prop=0;reason=core-unavailable;checkpoint=" READONLY\)/,
  );
  assert.match(
    backendCpp,
    /setModerationCapabilityState\(\s*modules\(\)\.palace_core\.moderationCapabilityStatus\(\)\);/,
  );
  assert.match(mainQml, /readonly property string moderationCapabilityState:/);
  assert.match(
    mainQml,
    /readonly property bool canBanUser:[\s\S]{0,280}authority"\)\s*=== "finalized"[\s\S]{0,280}can_ban_user"\)\s*=== "1"/,
  );
  assert.match(mainQml, /authority"\)\s*=== "local-committed"/);
  assert.match(
    mainQml,
    /readonly property bool canBanProp:[\s\S]{0,280}authority"\)\s*=== "finalized"[\s\S]{0,280}can_ban_prop"\)\s*=== "1"/,
  );

  const banUserOffset = roomUtilityQml.indexOf('objectName: "palaceBanUserButton"');
  const delegateOffset = roomUtilityQml.indexOf(
    'objectName: "palaceDelegateModeratorButton"',
  );
  const footerOffset = roomUtilityQml.indexOf(
    'objectName: "palaceModerationUserFooter"',
  );
  const banPropOffset = roomUtilityQml.indexOf(
    'objectName: "palaceBanAssignedPropButton"',
  );
  const trashOffset = mainQml.indexOf('objectName: "palacePropTrash"');
  const assetsOffset = roomToolbarQml.indexOf(
    'objectName: "palaceBackgroundModerationButton"',
  );
  assert.ok(
    banUserOffset >= 0
      && delegateOffset > banUserOffset
      && footerOffset < delegateOffset
      && banPropOffset > footerOffset,
  );
  assert.ok(trashOffset >= 0 && assetsOffset >= 0);
  const banUserBlock = roomUtilityQml.slice(banUserOffset, banPropOffset);
  const banPropBlock = roomUtilityQml.slice(banPropOffset, banPropOffset + 600);
  const trashBlock = mainQml.slice(trashOffset, trashOffset + 800);
  const assetsBlock = roomToolbarQml.slice(
    assetsOffset,
    roomToolbarQml.indexOf("onClicked:", assetsOffset),
  );
  assert.match(banUserBlock, /visible:\s*utility\.app\.canBanUser/);
  assert.match(banUserBlock, /enabled:\s*utility\.app\.canBanUser/);
  const footerBlock = roomUtilityQml.slice(footerOffset, delegateOffset);
  assert.match(footerBlock, /height:\s*70/);
  assert.match(banPropBlock, /visible:\s*utility\.app\.canBanProp/);
  assert.match(banPropBlock, /enabled:\s*utility\.app\.canBanProp/);
  assert.match(trashBlock, /visible:\s*root\.canBanProp/);
  assert.match(trashBlock, /enabled:\s*root\.canBanProp/);
  assert.doesNotMatch(assetsBlock, /canBan(User|Prop)/);
});

test("moderation roster scrolls inside fixed operator panel", () => {
  const panelOffset = roomUtilityQml.indexOf('objectName: "palaceModerationPanel"');
  const rosterOffset = roomUtilityQml.indexOf('objectName: "palaceModerationRoster"');
  const propOffset = roomUtilityQml.indexOf(
    'objectName: "palaceBanAssignedPropButton"',
  );
  const statusOffset = roomUtilityQml.indexOf('objectName: "palaceModerationStatus"');
  assert.ok(panelOffset >= 0 && rosterOffset >= 0);
  assert.ok(panelOffset < rosterOffset);
  assert.ok(rosterOffset < propOffset);
  assert.ok(propOffset < statusOffset);
  const rosterBlock = roomUtilityQml.slice(rosterOffset, rosterOffset + 900);
  assert.match(rosterBlock, /Flickable/);
  assert.match(
    rosterBlock,
    /contentHeight:\s*moderationRosterContent\.implicitHeight/,
  );
  assert.match(
    rosterBlock,
    /flickableDirection:\s*Flickable\.VerticalFlick/,
  );
  assert.match(
    rosterBlock,
    /Repeater\s*\{[\s\S]{0,180}model:\s*utility\.app\.participants\.length/,
  );
});

test("operator utilities keep selected-user moderation and keyboard access", () => {
  assert.match(mainQml, /property string selectedModerationUserId:\s*""/);
  assert.match(mainQml, /function selectedModerationUser\(\)/);
  assert.match(mainQml, /function selectedModerationUserName\(\)/);
  assert.match(
    mainQml,
    /onParticipantsChanged:\s*syncSelectedModerationUser\(\)/,
  );
  assert.match(
    mainQml,
    /function syncSelectedModerationUser\(\)[\s\S]{0,520}selectedModerationUserId = participantUserId/,
  );

  const statusUserOffset = mainQml.indexOf(
    'objectName: "palaceStatusSelectedUser"',
  );
  const statusBanOffset = mainQml.indexOf(
    'objectName: "palaceStatusBanUserButton"',
  );
  assert.ok(statusUserOffset >= 0 && statusBanOffset > statusUserOffset);
  const statusUserBlock = mainQml.slice(statusUserOffset, statusBanOffset);
  const statusBanBlock = mainQml.slice(statusBanOffset, statusBanOffset + 420);
  const statusBanBlockLong = mainQml.slice(
    statusBanOffset,
    statusBanOffset + 720,
  );
  assert.match(statusUserBlock, /text: root\.selectedModerationUserName\(\)\.length > 0/);
  assert.match(statusUserBlock, /onClicked:\s*root\.userListOpen = true/);
  assert.match(statusBanBlock, /visible:\s*root\.canBanUser/);
  assert.match(
    statusBanBlockLong,
    /onClicked:\s*root\.banUser\(subjectUserId\)/,
  );

  const rosterOffset = roomUtilityQml.indexOf(
    'objectName: "palaceModerationRoster"',
  );
  const footerOffset = roomUtilityQml.indexOf(
    'objectName: "palaceModerationUserFooter"',
  );
  const banUserOffset = roomUtilityQml.indexOf(
    'objectName: "palaceBanUserButton"',
  );
  const banPropOffset = roomUtilityQml.indexOf(
    'objectName: "palaceBanAssignedPropButton"',
  );
  assert.ok(rosterOffset >= 0 && footerOffset > rosterOffset);
  assert.ok(banUserOffset > footerOffset && banPropOffset > banUserOffset);
  assert.equal(
    [...roomUtilityQml.matchAll(/objectName:\s*"palaceBanUserButton"/g)].length,
    1,
  );

  const rosterBlock = roomUtilityQml.slice(rosterOffset, footerOffset);
  assert.match(rosterBlock, /delegate:\s*Button/);
  assert.match(rosterBlock, /objectName:\s*"palaceModerationRosterUser"/);
  assert.match(rosterBlock, /checkable:\s*true/);
  assert.match(
    rosterBlock,
    /checked:\s*utility\.app\.selectedModerationUserId[\s\S]{0,100}participantUserId/,
  );
  assert.match(
    rosterBlock,
    /onClicked:\s*utility\.app\.selectedModerationUserId\s*=\s*participantUserId/,
  );
  assert.match(rosterBlock, /Accessible\.name:\s*"Select " \+ participantName/);

  const footerBlock = roomUtilityQml.slice(footerOffset, banPropOffset);
  assert.match(
    footerBlock,
    /property string subjectUserId:\s*utility\.app\.selectedModerationUserId/,
  );
  assert.match(footerBlock, /visible:\s*utility\.app\.canBanUser/);
  assert.match(
    footerBlock,
    /onClicked:\s*utility\.app\.banUser\(subjectUserId\)/,
  );

  const participantSelectOffset = participantViewQml.indexOf(
    'objectName: "palaceParticipantSelect"',
  );
  assert.ok(participantSelectOffset >= 0);
  const participantSelectBlock = participantViewQml.slice(
    participantSelectOffset,
    participantSelectOffset + 640,
  );
  assert.match(
    participantSelectBlock,
    /anchors\.fill:\s*remoteAvatar/,
  );
  assert.match(
    participantSelectBlock,
    /onClicked:[\s\S]{0,120}participantView\.app\.selectedModerationUserId[\s\S]{0,80}= participantView\.participantUserId/,
  );

  assert.match(mainQml, /function closeActiveUtilityPanel\(\)/);
  assert.match(
    mainQml,
    /Shortcut\s*\{[\s\S]{0,220}sequence:\s*"Esc"[\s\S]{0,220}onActivated:\s*root\.closeActiveUtilityPanel\(\)/,
  );
  assert.match(mainQml, /function focusChatWhenUnobstructed\(\)/);
  assert.match(
    mainQml,
    /function focusChatWhenUnobstructed\(\)[\s\S]{0,240}roomChatBar\.focusInput\(\)/,
  );

  const roomControlsQml = roomToolbarQml + chatBarQml;
  for (const [objectName, accessibleName] of [
    ["palaceToolboxDoor", "Door / room exit"],
    ["palaceToolboxRooms", "Rooms"],
    ["palaceMoveUp", "Move up"],
    ["palaceMoveLeft", "Move left"],
    ["palaceMoveRight", "Move right"],
    ["palaceMoveDown", "Move down"],
    ["palaceWearAssignedProp", "Wear assigned prop"],
    ["palaceRemoveAssignedProp", "Remove worn prop"],
  ]) {
    const offset = roomControlsQml.indexOf(`objectName: "${objectName}"`);
    assert.ok(offset >= 0, `${objectName} selector remains available`);
    assert.match(
      roomControlsQml.slice(offset, offset + 360),
      new RegExp(`Accessible\\.name:\\s*"${accessibleName}"`),
    );
  }
  assert.match(
    chatBarQml,
    /objectName:\s*"palaceChatInput"[\s\S]{0,260}Accessible\.name:\s*"Chat message"/,
  );
});

test("prop placement uses bounded layer presets and click preview", () => {
  assert.match(mainQml, /function validPropLayer\(\s*value\s*\)/);
  assert.match(mainQml, /function clampAnchorToAsset\(\s*value,\s*sourceExtent\s*\)/);
  assert.match(mainQml, /function propPreviewScale\(\s*sourceWidth,\s*sourceHeight\s*\)/);
  assert.match(mainQml, /function setPropDraftAnchorFromPreview\(/);
  assert.match(
    mainQml,
    /function propDraftReady\(\)[\s\S]{0,220}validPropLayer\(propDraftLayer\)/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /model:\s*\["head",\s*"body",\s*"hand",\s*"back"\]/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /onClicked:\s*app\.propDraftLayer = modelData/,
  );
  assert.match(assetAuthoringPanelQml, /objectName:\s*"palaceAssetPropPreview-"\s*\+\s*backgroundCard\.handle/);
  assert.match(assetAuthoringPanelQml, /objectName:\s*"palaceAssetPropPreviewHit-"\s*\+\s*backgroundCard\.handle/);
  assert.match(
    assetAuthoringPanelQml,
    /setPropDraftAnchorFromPreview\([\s\S]{0,220}mouse\.x,[\s\S]{0,120}mouse\.y/,
  );
  assert.match(
    assetAuthoringPanelQml,
    /Pick layer, then click prop preview to set hot spot/,
  );
});
