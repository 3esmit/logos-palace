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
  assert.match(mainQml, /active:\s*root\.backgroundModerationOpen/);
  assert.match(mainQml, /objectName:\s*"palaceToolbox"/);
  assert.match(mainQml, /objectName:\s*"palaceStatusStrip"/);
  assert.match(mainQml, /objectName:\s*"palaceInputStrip"/);
  assert.match(mainQml, /objectName:\s*"palacePropBag"/);
  assert.match(mainQml, /objectName:\s*"palacePropTrash"/);
  assert.match(mainQml, /objectName:\s*"palacePropBagPanel"/);
  assert.match(mainQml, /objectName:\s*"palaceUserListToggle"/);
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
    mainQml,
    /objectName:\s*"palaceRoomBackground"[\s\S]{0,420}image:\/\/basecamp-verified\//,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomBackgroundPlaceholder"[\s\S]{0,800}roomBackground\.status !== Image\.Ready/,
  );
});

test("local development keeps its unavailable public finality visible", () => {
  assert.match(
    mainQml,
    /readonly property bool localDevelopmentMode:[\s\S]{0,180}locallyCommittedAuthority[\s\S]{0,180}localDevelopmentProfile/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceLocalDevelopmentIndicator"[\s\S]{0,180}visible:\s*root\.localDevelopmentMode/,
  );
  assert.match(mainQml, /Local development · public finality unavailable/);
});

test("creator onboarding retries only sequencer visibility races", () => {
  assert.match(
    mainQml,
    /function retryableCreatorActionRejection\(receipt\)/,
  );
  assert.match(
    mainQml,
    /rejected=lez-stable-account-read;reason=/,
  );
  assert.match(
    mainQml,
    /rejected=lez-observation;reason=/,
  );
  assert.match(mainQml, /transaction-not-materialized/);
  assert.match(
    mainQml,
    /updateCreatorActionRejection\(receipt\)/,
  );
  assert.match(
    mainQml,
    /Waiting for LEZ to expose the new Palace…/,
  );
});

test("creator onboarding materializes the entry-room state before enabling its door", () => {
  assert.match(uiRep, /SLOT\(QString createInitialRoomState\(\)\)/);
  assert.match(
    backendCpp,
    /QString LogosPalaceUiBackend::createInitialRoomState\(\)[\s\S]{0,280}modules\(\)\.palace_core\.createInitialRoomState\(\)/,
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
  assert.match(mainQml, /function prepareInitialRoomState\(\)/);
  assert.match(mainQml, /function createInitialRoomState\(\)/);
  assert.match(
    mainQml,
    /onboardingCreatorActionId === "0"[\s\S]{0,180}prepareInitialRoomState\(\)/,
  );
  const createInitialRoomStateStart = mainQml.indexOf(
    "function createInitialRoomState()",
  );
  const createInitialRoomStateEnd = mainQml.indexOf(
    "function activateOnboarding()",
    createInitialRoomStateStart,
  );
  assert.ok(
    createInitialRoomStateStart >= 0
      && createInitialRoomStateEnd > createInitialRoomStateStart,
  );
  const createInitialRoomState = mainQml.slice(
    createInitialRoomStateStart,
    createInitialRoomStateEnd,
  );
  assert.match(createInitialRoomState, /backend\.createInitialRoomState\(\)/);
  assert.match(createInitialRoomState, /confirming-initial-room-state/);
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
  assert.match(mainQml, /Continue room setup/);
});

test("joining an existing Palace imports its user-shared Storage catalog before opening", () => {
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
    mainQml,
    /objectName:\s*"palaceOnboardingStorageCatalog"[\s\S]{0,520}maximumLength:\s*16384/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceOnboardingStoragePeerEndpoint"[\s\S]{0,520}maximumLength:\s*16384/,
  );
  assert.match(
    mainQml,
    /readonly property string sharedStorageCatalog:[\s\S]{0,160}onboardingBundleStatus/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceSharedStorageCatalog"[\s\S]{0,420}readOnly:\s*true/,
  );
  assert.match(
    mainQml,
    /readonly property string sharedStoragePeerEndpoint:[\s\S]{0,100}onboardingStoragePeerEndpoint/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceSharedStoragePeerEndpoint"[\s\S]{0,420}readOnly:\s*true/,
  );
  const joinStart = mainQml.indexOf("function openExistingPalace()");
  const joinEnd = mainQml.indexOf("function enterCreatorMode()", joinStart);
  assert.ok(joinStart >= 0 && joinEnd > joinStart);
  const joinFlow = mainQml.slice(joinStart, joinEnd);
  assert.match(joinFlow, /backend\.connectStorage\(\)/);
  assert.match(joinFlow, /backend\.connectStoragePeer\(/);
  assert.match(joinFlow, /backend\.fetchMvpStorageBundle\(/);
  assert.match(joinFlow, /pollRoomSetupPublication\(\)/);
  assert.match(mainQml, /function parseStoragePeerEndpoint\(encoded\)/);
  assert.match(
    mainQml,
    /function updateExistingStorageCatalog\(receipt\)[\s\S]{0,700}openExistingPalaceAfterCatalog\(\)/,
  );
  assert.match(mainQml, /fetching-storage-catalog/);
  assert.match(mainQml, /Shared room catalog \(required for an existing Palace\)/);
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
  assert.match(mainQml, /objectName:\s*"palaceConnectStorage"/);
  assert.match(
    mainQml,
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
  assert.match(
    mainQml,
    /function canvasPixelToProtocol\s*\(\s*pixel,\s*inset,\s*usableSpan\s*\)/,
  );
  assert.match(
    mainQml,
    /function canvasPixelsToProtocol\s*\(\s*pixelX,\s*pixelY\s*\)/,
  );
  assert.match(mainQml, /objectName:\s*"palaceRoomMoveSurface"/);
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomMoveSurface"[\s\S]{0,220}z:\s*2/,
  );
  assert.match(
    mainQml,
    /var coordinate\s*=\s*root\.canvasPixelsToProtocol\(\s*mouse\.x,\s*mouse\.y\s*\)[\s\S]{0,120}root\.moveAvatar\(coordinate\.x,\s*coordinate\.y\)/,
  );

  const moveSurfaceOffset = mainQml.indexOf('objectName: "palaceRoomMoveSurface"');
  const participantOffset = mainQml.indexOf('objectName: "palaceParticipants"');
  const doorOffset = mainQml.indexOf('objectName: "palaceRoomDoor"');
  const doorBlock = mainQml.slice(
    doorOffset,
    mainQml.indexOf("onClicked:", doorOffset),
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
  assert.match(mainQml, /objectName:\s*"palaceRoomListPanel"/);
  assert.match(mainQml, /objectName:\s*"palaceRoomListAtrium"/);
  assert.match(mainQml, /objectName:\s*"palaceRoomListLounge"/);
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomListAtrium"[\s\S]{0,420}root\.selectFixedRoom\("atrium"\)/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceRoomListLounge"[\s\S]{0,520}root\.selectFixedRoom\("lounge"\)/,
  );
  assert.match(mainQml, /function selectFixedRoom\s*\(\s*roomId\s*\)/);
  assert.match(
    mainQml,
    /if \(selectedRoom === currentRoom\)[\s\S]{0,360}watchedActionReceipt\s*=\s*"ok=room-current;[\s\S]{0,180}return watchedActionReceipt/,
  );
  assert.match(
    mainQml,
    /selectedRoom === "lounge"[\s\S]{0,180}root\.gate5UseDoor\(\)/,
  );
  assert.match(
    mainQml,
    /selectedRoom === "atrium"[\s\S]{0,220}backend\.enterRoom\("atrium"\)/,
  );
  assert.match(
    mainQml,
    /objectName:\s*"palaceToolboxRooms"[\s\S]{0,420}root\.roomListOpen\s*=\s*!root\.roomListOpen/,
  );
  assert.match(
    mainQml,
    /id: roomDoor[\s\S]{0,160}objectName:\s*"palaceRoomDoor"/,
  );
  assert.match(
    doorBlock,
    /anchors\.bottomMargin:\s*root\.roomCanvasDoorBottomMargin/,
  );
  assert.match(
    doorBlock,
    /background:\s*Rectangle[\s\S]*contentItem:\s*Text/,
  );
  assert.match(
    mainQml,
    /property int cardWidth:\s*Math\.max\(\s*320,/,
  );
  assert.match(mainQml, /text: "Set Atrium"/);
  assert.match(mainQml, /text: "Set Lounge"/);
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
    /gate3PublishBundle\(\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /gate3PublishPng\(handle\)[\s\S]{0,220}if \(!canManageAssets\)\s*return rejectAssetAuthoringReadOnly\(\)/,
  );
  assert.match(
    mainQml,
    /ToolTip\.text:\s*root\.assetAuthoringReadOnlyMessage\(\)/,
  );
  assert.match(
    mainQml,
    /enabled:\s*root\.ready[\s\S]{0,160}root\.canManageAssets[\s\S]{0,160}!root\.assetImportRunning/,
  );
  assert.match(
    backendCpp,
    /setActivePropAsset\([\s\S]{0,240}refreshAssetAuthoringCapabilityState\(\);/,
  );
});

test("backend stops poll timers on teardown", () => {
  assert.match(backendH, /~LogosPalaceUiBackend\(\)/);
  assert.match(backendCpp, /stopPollingTimers/);
  assert.match(backendCpp, /m_deliveryPollTimer->stop\(\)/);
  assert.match(backendCpp, /m_nodeEvidencePollTimer->stop\(\)/);
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
    /humanModerationAuthority\(context, requiredCapability\)/,
  );

  assert.match(
    uiRep,
    /PROP\(QString moderationCapabilityState="authority=unavailable;can_ban_user=0;can_ban_prop=0;reason=core-unavailable;checkpoint=" READONLY\)/,
  );
  assert.match(
    backendCpp,
    /setModerationCapabilityState\(\s*modules\(\)\.palace_core\.moderationCapabilityStatus\(\)\);/,
  );
  assert.match(mainQml, /gate4ModerationCapabilityState:\s*moderationCapabilityState/);
  assert.match(
    mainQml,
    /readonly property bool canBanUser:[\s\S]{0,280}authority"\)\s*=== "finalized"[\s\S]{0,280}can_ban_user"\)\s*=== "1"/,
  );
  assert.match(mainQml, /authority"\)\s*=== "local-committed"/);
  assert.match(
    mainQml,
    /readonly property bool canBanProp:[\s\S]{0,280}authority"\)\s*=== "finalized"[\s\S]{0,280}can_ban_prop"\)\s*=== "1"/,
  );

  const banUserOffset = mainQml.indexOf('objectName: "palaceBanUserButton"');
  const banPropOffset = mainQml.indexOf(
    'objectName: "palaceBanAssignedPropButton"',
  );
  const trashOffset = mainQml.indexOf('objectName: "palacePropTrash"');
  const assetsOffset = mainQml.indexOf(
    'objectName: "palaceBackgroundModerationButton"',
  );
  assert.ok(banUserOffset >= 0 && banPropOffset >= 0);
  assert.ok(trashOffset >= 0 && assetsOffset >= 0);
  const banUserBlock = mainQml.slice(banUserOffset, banPropOffset);
  const banPropBlock = mainQml.slice(banPropOffset, banPropOffset + 600);
  const trashBlock = mainQml.slice(trashOffset, trashOffset + 800);
  const assetsBlock = mainQml.slice(
    assetsOffset,
    mainQml.indexOf("onClicked:", assetsOffset),
  );
  assert.match(banUserBlock, /visible:\s*root\.canBanUser/);
  assert.match(banUserBlock, /enabled:\s*root\.canBanUser/);
  assert.match(banPropBlock, /visible:\s*root\.canBanProp/);
  assert.match(banPropBlock, /enabled:\s*root\.canBanProp/);
  assert.match(trashBlock, /visible:\s*root\.canBanProp/);
  assert.match(trashBlock, /enabled:\s*root\.canBanProp/);
  assert.doesNotMatch(assetsBlock, /canBan(User|Prop)/);
});

test("moderation roster scrolls inside fixed operator panel", () => {
  const panelOffset = mainQml.indexOf('objectName: "palaceModerationPanel"');
  const rosterOffset = mainQml.indexOf('objectName: "palaceModerationRoster"');
  const propOffset = mainQml.indexOf(
    'objectName: "palaceBanAssignedPropButton"',
  );
  const statusOffset = mainQml.indexOf('objectName: "palaceModerationStatus"');
  assert.ok(panelOffset >= 0 && rosterOffset >= 0);
  assert.ok(panelOffset < rosterOffset);
  assert.ok(rosterOffset < propOffset);
  assert.ok(propOffset < statusOffset);
  const rosterBlock = mainQml.slice(rosterOffset, rosterOffset + 900);
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
    /Repeater\s*\{[\s\S]{0,180}model:\s*root\.participants\.length/,
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
    /onClicked:\s*root\.gate4BanUser\(subjectUserId\)/,
  );

  const rosterOffset = mainQml.indexOf(
    'objectName: "palaceModerationRoster"',
  );
  const footerOffset = mainQml.indexOf(
    'objectName: "palaceModerationUserFooter"',
  );
  const banUserOffset = mainQml.indexOf(
    'objectName: "palaceBanUserButton"',
  );
  const banPropOffset = mainQml.indexOf(
    'objectName: "palaceBanAssignedPropButton"',
  );
  assert.ok(rosterOffset >= 0 && footerOffset > rosterOffset);
  assert.ok(banUserOffset > footerOffset && banPropOffset > banUserOffset);
  assert.equal(
    [...mainQml.matchAll(/objectName:\s*"palaceBanUserButton"/g)].length,
    1,
  );

  const rosterBlock = mainQml.slice(rosterOffset, footerOffset);
  assert.match(rosterBlock, /delegate:\s*Button/);
  assert.match(rosterBlock, /objectName:\s*"palaceModerationRosterUser"/);
  assert.match(rosterBlock, /checkable:\s*true/);
  assert.match(
    rosterBlock,
    /checked:\s*root\.selectedModerationUserId[\s\S]{0,100}participantUserId/,
  );
  assert.match(
    rosterBlock,
    /onClicked:\s*root\.selectedModerationUserId\s*=\s*participantUserId/,
  );
  assert.match(rosterBlock, /Accessible\.name:\s*"Select " \+ participantName/);

  const footerBlock = mainQml.slice(footerOffset, banPropOffset);
  assert.match(
    footerBlock,
    /property string subjectUserId:\s*root\.selectedModerationUserId/,
  );
  assert.match(footerBlock, /visible:\s*root\.canBanUser/);
  assert.match(
    footerBlock,
    /onClicked:\s*root\.gate4BanUser\(subjectUserId\)/,
  );

  const participantSelectOffset = mainQml.indexOf(
    'objectName: "palaceParticipantSelect"',
  );
  assert.ok(participantSelectOffset >= 0);
  const participantSelectBlock = mainQml.slice(
    participantSelectOffset,
    participantSelectOffset + 640,
  );
  assert.match(
    participantSelectBlock,
    /anchors\.fill:\s*remoteAvatar/,
  );
  assert.match(
    participantSelectBlock,
    /onClicked:[\s\S]{0,120}root\.selectedModerationUserId[\s\S]{0,80}= participantDelegate\.participantUserId/,
  );

  assert.match(mainQml, /function closeActiveUtilityPanel\(\)/);
  assert.match(
    mainQml,
    /Shortcut\s*\{[\s\S]{0,220}sequence:\s*"Esc"[\s\S]{0,220}onActivated:\s*root\.closeActiveUtilityPanel\(\)/,
  );
  assert.match(mainQml, /function focusChatWhenUnobstructed\(\)/);
  assert.match(
    mainQml,
    /function focusChatWhenUnobstructed\(\)[\s\S]{0,240}chatInput\.forceActiveFocus\(\)/,
  );

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
    const offset = mainQml.indexOf(`objectName: "${objectName}"`);
    assert.ok(offset >= 0, `${objectName} selector remains available`);
    assert.match(
      mainQml.slice(offset, offset + 360),
      new RegExp(`Accessible\\.name:\\s*"${accessibleName}"`),
    );
  }
  assert.match(
    mainQml,
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
    mainQml,
    /model:\s*\["head",\s*"body",\s*"hand",\s*"back"\]/,
  );
  assert.match(
    mainQml,
    /onClicked:\s*root\.propDraftLayer = modelData/,
  );
  assert.match(mainQml, /objectName:\s*"palaceAssetPropPreview-"\s*\+\s*backgroundCard\.handle/);
  assert.match(mainQml, /objectName:\s*"palaceAssetPropPreviewHit-"\s*\+\s*backgroundCard\.handle/);
  assert.match(
    mainQml,
    /setPropDraftAnchorFromPreview\([\s\S]{0,220}mouse\.x,[\s\S]{0,120}mouse\.y/,
  );
  assert.match(
    mainQml,
    /Pick layer, then click prop preview to set hot spot/,
  );
});
