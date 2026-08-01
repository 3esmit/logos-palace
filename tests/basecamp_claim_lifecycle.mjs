import { createHash } from "node:crypto";
import {
  link,
  lstat,
  open,
  readFile,
  readdir,
  realpath,
  rename,
  unlink,
} from "node:fs/promises";
import { basename, dirname, join, resolve } from "node:path";

export const releaseProgramId =
  "e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61";
export const releaseRootId =
  "12ff117a38d756f132cf616cea36fa007653c3c99727c1b475503caa345cbf2a";

const claimSchema = "logos.palace.basecamp-active-run-claim";
const reportSchema = "logos.palace.basecamp-mvp-compiled-report";
const rollForwardSchema =
  "logos.palace.basecamp-active-run-roll-forward";
const completionSchema =
  "logos.palace.basecamp-active-run-completion";
const implementedGates = [
  "gate0",
  "gate1",
  "gate2",
  "gate3",
  "gate4",
  "gate5",
  "gate6",
];
const preGate3Phases = new Set([
  "gate0",
  "gate1",
  "gate2",
  "signal",
  "run-scope",
  "active-run-claim",
  "gate3-claim-transition",
]);
const sha256Pattern = /^[0-9a-f]{64}$/;
const sourceCommitPattern = /^[0-9a-f]{40}$/;
const narHashPattern = /^sha256-[A-Za-z0-9+/]{43}=$/;
const identityRegistrationAndIdleStorageProfile =
  "identity-registration-and-idle-storage-before-palace-write";
const identityRegistrationAndApprovalGuardedAssetsProfile =
  "identity-registration-and-approval-guarded-assets-before-palace-write";
const identityRegistrationAndPublishedAssetsProfile =
  "identity-registration-and-published-assets-before-palace-write";
// Creator sealed the MVP storage catalog and bound authored CIDs, then failed
// on peer fetch/retention before any LEZ palace write (root still uninitialized).
const identityRegistrationAndSealedMvpBundleProfile =
  "identity-registration-and-sealed-mvp-bundle-before-palace-write";
// The creator can be stopped after a sealed bundle has survived both local
// retention rounds. This remains before the first Palace-root write, but must
// not weaken the earlier sealed-bundle recovery profile.
const identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOfflineProfile =
  "identity-registration-and-sealed-mvp-bundle-retained-after-creator-offline-before-palace-write";
const storageCidPattern =
  /^(b[a-z2-7]{50,}|z[1-9A-HJ-NP-Za-km-z]{40,})$/;

export const auditedLegacyPreGate3 = Object.freeze({
  gitCommit: "1a61a457bb13e0c016f838a7fcd9c8820098a68a",
  snapshotNarHash:
    "sha256-KYD79MQnS+uVRZFA2YGung18ABWUV4k5UI5otIPD4uo=",
  snapshotNarSize: 6_270_528,
  snapshotRunnerSha256:
    "e706612c79cdd59a07166ed3ada3fc691505f96969eac1dfed41dcadf167e3ea",
  runtimeManifestSha256:
    "dceba39d0b74dde0319c586439e91e845351c400a8ddb7e27494265875e65eba",
  compiledReportSha256:
    "24d7366ed2aa6784535ffb1ad04f05cea498935eeaa8dbb6740562cad47b78d7",
  gate0ReportSha256:
    "efe27b0d87ab0fac5f0eb96d08189c456be6f5f070b3382af3b8576d415ef41e",
  gate1ReportSha256:
    "d22d60a15a1b7310d40048735abd3f6850ecf3fd9cddddfec588b8348a9027b3",
});

// Each entry is an exact recovery audit for one immutable Gate 3 run. Its
// report must prove the run stopped before its first public write.
export const auditedPrePublicWriteGate3Failures = Object.freeze([
  Object.freeze({
    gitCommit: "6866fe090a9e3b77625869606704bb6398d589a2",
    snapshotNarHash:
      "sha256-yVIq0V3ALbozFlZR4uNnYGuWnhXeeZnetsapy0jp9b8=",
    snapshotNarSize: 7_078_968,
    snapshotRunnerSha256:
      "7241b93c58f896958be425736b8918b676b9b6e0f0e68c5c34dbe017a0cd3fa9",
    runtimeManifestSha256:
      "debdf168bdb8e0e5de7b1750207eebd990db262d4ac5ad33836745ac235fad1c",
    compiledReportSha256:
      "c063493c5e101835a437eae85daa5afb29050fdf2d23a70cbe74d08323b398cb",
    gate3ReportSha256:
      "4fac76d54d2a69758b30473fd0fde42287961f4b8103ad964e6d8cef777a14e6",
    gate3Failure: "production LEZ a: rejected=lez-network-fingerprint",
    retirementStatus: "audited-fingerprint-rejection",
  }),
  Object.freeze({
    gitCommit: "39aeee81a23cd183d5cb821cdd0e20dcc836e9ba",
    snapshotNarHash:
      "sha256-u/3D3yuCmDaKoODNtagwaYC+0FGtX5WsvofWef7ZY8o=",
    snapshotNarSize: 7_101_152,
    snapshotRunnerSha256:
      "7241b93c58f896958be425736b8918b676b9b6e0f0e68c5c34dbe017a0cd3fa9",
    runtimeManifestSha256:
      "ecdbf4f480444ca6ced110c197e56c361341f46adef71b054c47c1d3e0b5ea72",
    compiledReportSha256:
      "3420ffd04626d0c6c1a709c5dc15882386ebd494e06634a65af7eaa496ab1a63",
    gate3ReportSha256:
      "91a3c4e96fba72d4c703ae6484b1cee19479af8e82c969fd645f53f0c06aae88",
    gate3Failure:
      "worker a: gate4StartLez receipt timeout: before=\"\" after=\"\" sequence=0->0",
    retirementStatus: "audited-pre-public-write-failure",
  }),
  Object.freeze({
    gitCommit: "2f18a99f9e48b8ce84b81ccdc3864e7a738634fe",
    snapshotNarHash:
      "sha256-4nZ7s5+fwYKqkivxsz4VNUqsZkV8lKdCnfRIyQUlSeg=",
    snapshotNarSize: 7_149_904,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "7fd09f10e24ba82a52eaefb72e6cf3ef762ab1ea7db4347dc5497b279c2b46fe",
    gate3ReportSha256:
      "4eccb0a1934e2d0c8fc18138155e3f1bf72bac92ff97c15f98c476a3318fa687",
    gate3Failure: "production LEZ a: ",
    retirementStatus: "audited-pre-public-write-failure",
  }),
  Object.freeze({
    gitCommit: "51375a2a136611f5c8b9dc490619ff7f6154babf",
    snapshotNarHash:
      "sha256-NVv+KyANhwtF1wk/oQ4nKBB+VVGVLCelNL30HP5Sbio=",
    snapshotNarSize: 7_152_184,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "a2a25bcdd6864ef638979f255d88f4784982365945faaf811a97cc27c50bc3a0",
    gate3ReportSha256:
      "18aae314e77ce1408c198b1cb78988399a276c3fdd62924f7d6bdf881df031df",
    gate3Failure:
      "worker a: gate4StartLez receipt timeout: before=\"\" after=\"\" sequence=0->1",
    retirementStatus: "audited-pre-public-write-failure",
  }),
  Object.freeze({
    gitCommit: "bc0fdf742d695d3750e30f7bd76125c0b9ba4b87",
    snapshotNarHash:
      "sha256-XrqEsotCFYBxsruXJUcc6EYWUZ4PweC0pXLmVun+T9E=",
    snapshotNarSize: 7_155_584,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "50c413d01dd965ad5a5e8cee0612494ec93f1acb1181fd53c87734e0d3db7da0",
    gate3ReportSha256:
      "6ff81a0b4ff1ec18e102051fd113387216aac211d8cf985523fcd422df247631",
    gate3Failure: "worker a: asset picker opened multiple dialogs",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndIdleStorageProfile,
  }),
  Object.freeze({
    gitCommit: "49d4aa58d08611417def4743ef62c0a4befa3f65",
    snapshotNarHash:
      "sha256-Gvf0HlnoEf+RYF6LAQJgE1y9OEzXNOBw/TU9NU8PYNg=",
    snapshotNarSize: 7_170_928,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "f6ce2bca3b69306f6b87f54b62d9212bbc2cbef279b5edbf7b05dd74c4adb161",
    gate3ReportSha256:
      "0aac75f279b12fcc9b183d9094a08295dae4845dea83dd35b377ec7b1bcd5e8c",
    gate3Failure: "worker a: asset picker import did not complete",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndIdleStorageProfile,
  }),
  Object.freeze({
    gitCommit: "85aac443fb5eafc1d74c61a8b791c4ed9ec4007c",
    snapshotNarHash:
      "sha256-MejD+MHN4A7asEdYOXuH8eSA2un5lDXzpG1wv5s50bg=",
    snapshotNarSize: 7_175_048,
    snapshotRunnerSha256:
      "264ac08a6cb5d5709eb85e90c9947107ba575284100863c96172d20b170cdba0",
    runtimeManifestSha256:
      "5673105501814c2389f03de87c2811cc773d7817ebaa580fcf5b4e3ce114a808",
    compiledReportSha256:
      "fa7d09f9239cbf64f9f369f2653a45fbf7bc87787dffbbadbd739718853fd917",
    gate3ReportSha256:
      "072b2d6021d3701e889191573a251e47fb4c7bf5f6e2c6f642e8d9bd678686cd",
    gate3Failure: "worker a: moderation asset upload failed",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndApprovalGuardedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "3abf96e49be4ec0a4198d18ecbce1ec22aa39977",
    snapshotNarHash:
      "sha256-0o6SiE2U2F9V3uq++PRy/ttc1pBxTRmufAbIxcVwBg4=",
    snapshotNarSize: 7_194_096,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "93bd35db8ac8f54a49c0459c63d663a47ac8a574c3276707490f742fcd321186",
    compiledReportSha256:
      "261ae2cd4d2686689ad158539abf91c28e44e2a5dc06fc83dfcac39ff13e78d8",
    gate3ReportSha256:
      "12ce7c7a22240e4fd043f735f284e3199e5f9cc40fc1e263d1c96b82b9d065d6",
    gate3Failure:
      "worker a: moderation approval and upload timed out: publication=not-uploaded",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndApprovalGuardedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "7d1bddcba5953d5ba730b8a557e2c750484b70ec",
    snapshotNarHash:
      "sha256-SRwuAkp3z+7/YLwybqGO4bM/tHcrgzHSQ77re7jrsWQ=",
    snapshotNarSize: 7_194_840,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "3b49477c76c3905b7116d74d4ac6b92167e6ae7f6641f8e3e2abf4ec3c9ec4a6",
    compiledReportSha256:
      "263935bd74c892b6c9d4538f861a138ba7336b5a563ad658774ea2986eba155f",
    gate3ReportSha256:
      "3f5e45c533e399e0ad820b7e1775934e1a6472d0c016cf23f24b4bac7008e18a",
    gate3Failure:
      "worker a command timed out: assignRoomBackgroundFromModeration",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "2364d1f2775336e5d9b7fda2df582787659772cd",
    snapshotNarHash:
      "sha256-4j+xJKY/EA1jj2fhE672g8xsqZpBUgPNCteor9meXJE=",
    snapshotNarSize: 7_205_088,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "1e3fcc734a47c1ec96e97c4acb4d070b3b0f03663259dba2fa6e72d6a0f4ff92",
    compiledReportSha256:
      "d8a038531f54baaf811ae492c4eb2142183a18d7a6030c43924e7ff5a49289f3",
    gate3ReportSha256:
      "b8c9d13351f723f343b201daf52e058be37e7768bc0bf754ef45e9d3cfd16ad6",
    gate3Failure:
      "worker a: moderation atrium assignment timed out: publication=published",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "0613088c9ba4bdde9d8ec468ab4461f0b3c94b4c",
    snapshotNarHash:
      "sha256-q4ig/DmV8nt5Mnp3RVl6PBvZhnuG8NgFkw+EsFiQpzs=",
    snapshotNarSize: 7_209_128,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "6a45b3f0f5b31ccb14ff9da412c7d04f5ebf517a1c4a3839c9aaa111d31cd765",
    compiledReportSha256:
      "87068dc4e747d90ceeb5b8f571268c0a87c928c6b192062891a2e3e9ccd4c13d",
    gate3ReportSha256:
      "84e366a4bf60dc7983ec83a442bb493e349d9c2a2bd123b75d0ea7df7cfe9c3a",
    gate3Failure: "worker a: asset authoring evidence is invalid",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "75625d0c46257e25bc46b05bbcccba30721d5247",
    snapshotNarHash:
      "sha256-Ul1sukR5p7uAFFjMzakpkZy3KqmGI2Gf2cZtR0L2eoc=",
    snapshotNarSize: 7_211_176,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "61f28675f5eefab07cc16ab8199068f7dbb2ced168838dda4745b0843cbe5e47",
    compiledReportSha256:
      "a493b4d3fb1ea0ed565f1fb5404c49422b3d90fc8e3f45936294284746827149",
    gate3ReportSha256:
      "eaf82fadf3336dda1073a6fb732cd62767ed7b40e6b68794ff1f4beb1072789b",
    gate3Failure:
      "worker a: gate3PublishBundle receipt timeout: before=\"state=missing;published=0;verified=0;total=0;retention=missing;retention_round=0;source=none;native_available=0;native_total=0\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=44785;synced_height=44785;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=100->101",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "549cc0a014fb1aa78b0dbba7863ceb398cb90851",
    snapshotNarHash:
      "sha256-NBuVvmZ2LfnB2BkSvkhvsuKPPN/VeR9bdPp5C4ciOPE=",
    snapshotNarSize: 7_228_240,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "bc8acc4a559fec73e9756cf71e580cc8bf40baa80ab1ab0046422f7a9cf2af37",
    compiledReportSha256:
      "01b9ad842d98a0d1aa43469f916ac4c4eb46d2fae4e6d559aa62cfb70488559b",
    gate3ReportSha256:
      "21b978b96de8ba156b5945d4de1fad890e26c3e0d1764e062482abfd157dade7",
    gate3Failure:
      "worker a: gate3PublishBundle receipt timeout: before=\"state=missing;published=0;verified=0;total=0;retention=missing;retention_round=0;source=none;native_available=0;native_total=0\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=44863;synced_height=44863;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=100->101",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "7bc6de1cc0020673f61548df1f8a324c8182410f",
    snapshotNarHash:
      "sha256-RmLMTmM+KqVWKQjmT3D8+zyEhOzPF13UolrtVDHGO14=",
    snapshotNarSize: 7_226_984,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "4b799a5e0362e9c41a78d5549c04811ba323024eec0f7adeab30ce07ed052bf5",
    compiledReportSha256:
      "0a66ddad541bd08b6b7e45ede0432a00b01bcea2ed26ca05ebd6498172d8306c",
    gate3ReportSha256:
      "3b2081778c446777a06823775b7bc62d8d1d6b30aa241fc62ecd1b037d3b40c7",
    gate3Failure:
      "worker a: gate3PublishBundle receipt timeout: before=\"state=missing;published=0;verified=0;total=0;retention=missing;retention_round=0;source=none;native_available=0;native_total=0\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=44899;synced_height=44899;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=100->101",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "3ef866e981372e5d5d1f069ab18010170876da50",
    snapshotNarHash:
      "sha256-nH8FWRBJr8ZkJ0pwxrZwU5xtQ/R2OGp3iX+gDiLmbNk=",
    snapshotNarSize: 7_236_960,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "3261aa5901143bfae5b52a24f6c46e0db335806d6805d6376750f61e1d5b1e7c",
    compiledReportSha256:
      "281f1c7d66a71b3284b108f0cb5440a98a543bce190cb94e29a824e274b86d7e",
    gate3ReportSha256:
      "47b9d45fdab138fe09c5aac9ef0cd67d76f57a17e6743a3fc89ecaaf0348d269",
    gate3Failure:
      "authored asset is not active graph leaf: room-background",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndPublishedAssetsProfile,
  }),
  Object.freeze({
    gitCommit: "f84d18ab433ffbce7a30eb7ff64af1b4fff0aed1",
    snapshotNarHash:
      "sha256-V65x+QQMw6q8HRA2rnjJKyelVWmzCFNEaHeJ09548Fc=",
    snapshotNarSize: 7_240_496,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "52a938a8ff4394070e9ea5be10058827763ec0ebcbe936cee5fe413e327b2a6b",
    compiledReportSha256:
      "b73285fbde700b3d5b1fa04f2dc48bca0d4edce467324b5059eb519f3a9bbc6d",
    gate3ReportSha256:
      "af9e7ae4d8507d0ca9b2f84cdf3f3069f1f6d46fb5e1796782b59a766fc78e52",
    gate3Failure:
      "worker b: gate3FetchBundle receipt timeout: before=\"state=missing\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=45024;synced_height=45024;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=90->91",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  Object.freeze({
    gitCommit: "8ad66d6ad1302f110b61b94b4999964675fa9aff",
    snapshotNarHash:
      "sha256-enHXiL7UJAUpwVGYoYaEIS1zDWKNRXb5wjRXil7nh4U=",
    snapshotNarSize: 7_246_360,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "52a938a8ff4394070e9ea5be10058827763ec0ebcbe936cee5fe413e327b2a6b",
    compiledReportSha256:
      "aeb6ae7381e8b58d1dda32162edbc058ed60d2e1e4280e18910e953099799115",
    gate3ReportSha256:
      "ddcfb68c8f20284f914195303fcb19418b28b6f4b728b594c5b8e968c5257061",
    gate3Failure:
      "worker b: gate3FetchBundle receipt timeout: before=\"state=missing\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=45056;synced_height=45056;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=90->91",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.EEIV4ITA @ e5e6898: creator sealed MVP + storage mesh dials, peer B
  // gate3FetchBundle returned degraded (published=8 verified=0) before LEZ write.
  Object.freeze({
    gitCommit: "e5e68981d5cffc75380df198172c7e37bc611e3e",
    snapshotNarHash:
      "sha256-Zg8fKTCB5vbyHmQR75SwKJaK2YZAEhjPSpfFHjvgZn4=",
    snapshotNarSize: 7_263_432,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "b993ce9b2e4bf73320469016cea0a7c13dba765a7c72293d40b2f1949f3f5baa",
    compiledReportSha256:
      "eb28dd9f44c1f9e213771b4c0d79e1ebb976e61d7a1439d0f2597fb6e8e768e7",
    gate3ReportSha256:
      "0056a01edb5b498a52ff8b9f9004c7f071956bfc756be1d6b2aca0898555cc2d",
    gate3Failure:
      "b did not expose fetching state: ok;state=degraded;published=8;verified=0;total=8;retention=degraded",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.FgHCICxI @ 70744a5: remesh pre-fetch still hung gate3FetchBundle on
  // logos.test GetProviders (empty after= timeout) before Palace write.
  Object.freeze({
    gitCommit: "70744a51ed16efed9ec17d3bafe8bd0d08b83bcc",
    snapshotNarHash:
      "sha256-3UYH0BFuqYj2u70qWpXqy3T8SIERdw6KkGK1f5xIn4Y=",
    snapshotNarSize: 7_267_256,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "7cbf21e47dc170d13bfd7b2fb82263c26265abeb914402364f800d020c90ea80",
    compiledReportSha256:
      "d29cc2caa238cb7ae6131fdfecd2b1ebc174f8c838d2bf4c3f0b0a36701d0f85",
    gate3ReportSha256:
      "0cace6291d714cd0aacbc0600a3aca517050d44488878f39ae1f8f3acc530e86",
    gate3Failure:
      "worker b: gate3FetchBundle receipt timeout: before=\"state=missing\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=45139;synced_height=45139;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=94->95",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.cFb7BJNx @ 2fba7ca: private mesh + remesh still hung gate3FetchBundle
  // (downloadToUrlV2/manifest on the same invoke) before Palace write.
  Object.freeze({
    gitCommit: "2fba7cafcf726a065a23541c7d8c9929f4270224",
    snapshotNarHash:
      "sha256-jcrzBdipetHSogJsEsYtL2Qa2W9XfQNAEJlDEjKShbs=",
    snapshotNarSize: 7_269_480,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "7cbf21e47dc170d13bfd7b2fb82263c26265abeb914402364f800d020c90ea80",
    compiledReportSha256:
      "f0e601614cb47fc8a207f238cbd915d6803c107408712a952bc74e64519e9263",
    gate3ReportSha256:
      "2a9eef283a4c323a909b01a503df1ba52b13a42eeb7fdb1e9fd9723d8a2fe3c4",
    gate3Failure:
      "worker b: gate3FetchBundle receipt timeout: before=\"state=missing\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=45171;synced_height=45171;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=17->18",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.nlw8ONqt @ 13a8f76: deferred fetch returned fetching but verified stayed 0
  // for 10m (network download never completed) before Palace write.
  Object.freeze({
    gitCommit: "13a8f766c46b4e3bb052372d2bcf41ecb820c71c",
    snapshotNarHash:
      "sha256-ol4rJQi9UWelF6Pxn7r+nVmQb/l/kRb3lQbzaJeIF8M=",
    snapshotNarSize: 7272640,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "aed99597654e53649f861fac365131c963173bd47de152e52ad03b8ff617365f",
    compiledReportSha256:
      "9a66aff4eb7a58f0dbf9bf32b4668932bf7a715247c2dfd057f6cb1300358340",
    gate3ReportSha256:
      "a4112f57fca3f80155710b723002975d75dd3792012db6e1fcbf54b5828a18e7",
    gate3Failure:
      "fetch exact MVP bundle on b timed out after 600000 ms: state=fetching;published=8;verified=0;total=8;retention=missing;retention_round=0;source=network;native_available=0;native_total=8;catalog=bG9nb3MtcGFsYWNlLW12cC1zdG9yYWdlLWNhdGFsb2ctdjEKdmVyc2lvbj0xCnJvb3Q9cGFsYWNlLTEKb2JqZWN0cz04Cm9iamVjdD1iYWNrZ3JvdW5kLWF0cml1bTtiYWNrZ3JvdW5kX3BuZztpbWFnZS9wbmc7ekR2WlJ3emt3UDNVdzRxd2RNNHJ6RXZOVFU4b0ExQlF1SndjTndxeE1LZ3JVdWJBU3k4ZDsxMTc2OTE2O2Y4MjdiNjVlZmVjZDVhNjIxMjU0ZGM4YjZjMmY0MTU0MWI2ZmI2MzI2Njc2YjVmYTFlZDNhODkyYjllN2JjMzcKb2JqZWN0PWJhY2tncm91bmQtbG91bmdlO2JhY2tncm91bmRfcG5nO2ltYWdlL3BuZzt6RHZaUnd6bTFRdFQyb0o2WTltZUp6YlNud3JpOWNLellUelVjd3RqWlJ6NDIyR2Via29ROzE2NzEzNjk7MDA1ZWRiYzY1NzA2Mjk3Y2VmYzZhYzZkZWVkZDc3ZTI5Zjc1NTE1OTI3MWIyZWM1YmI5OWI4YzlhODRjNjZmOApvYmplY3Q9cm9vbS1hdHJpdW0tbWV0YWRhdGE7cm9vbV9tZXRhZGF0YTthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLnJvb20tdjE7ekR2WlJ3em0zZk12R1NyZDF3S0xDenZ6aG1FOHhzdU5OeGFxbnBQQXg1WkFpc2g4dEsxaTsxOTU7MDlmMmI5MWE2MzE0NzBiNGRjNWI1ZDVkYzM4MTIwOTcwZjM5ZGQ4MDljMDQzYWFlYzhlN2IyNzBhZjA3NGMyYgpvYmplY3Q9cm9vbS1sb3VuZ2UtbWV0YWRhdGE7cm9vbV9tZXRhZGF0YTthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLnJvb20tdjE7ekR2WlJ3em1EOHN6M0xSRzhBMkVMVWplWjhkUVVLa21zeWJFcHEyajNkZFVQQnBIMjNKNzsxOTU7M2Q4MmJkNDRhYmNjZTAzMzg4ZjgyZTExMzA2OGM5N2E4ZjY5ZDE0Y2RhNDllOTQ4Zjg1ZjAzYWQ4ODcyM2RhNwpvYmplY3Q9c2NyaXB0LWRvb3I7c2NyaXB0X2J1bmRsZTthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLnNjcmlwdC12MTt6RHZaUnd6a3huTThtSm1KTVF4bjJjUXpZeTlrQ1B3clNtSnpIeHhXSENvSGFUcFUyV3VGOzQ3Ozk5YTRkMmQxNjIxYTUwMTRlYTk2Yzk2OTgxYzc4MzNkNGYwYTI4NTU5ZDU5YzBmNTdiN2UyMmYwMDdmNjUyNDUKb2JqZWN0PXJvb20tYXRyaXVtO3Jvb21fbWFuaWZlc3Q7YXBwbGljYXRpb24vdm5kLmxvZ29zLXBhbGFjZS5jYXRhbG9nLW1hbmlmZXN0LXYxO3pEdlpSd3ptQjF0WHQ0SHpBRVRHczEyY2hUZnpvWmU1ZVZYMkpMck1aS252YWJ1UFo2WFU7NTIwOzUyNGQzZmUzZTQ2MmI5ODMwZmI0NmU3MWY0MWEwOGZjNTVmMTVjNGE0N2ZmMDQwZWU5OGMwMGQ0MDdkNGQ0YTYKb2JqZWN0PXJvb20tbG91bmdlO3Jvb21fbWFuaWZlc3Q7YXBwbGljYXRpb24vdm5kLmxvZ29zLXBhbGFjZS5jYXRhbG9nLW1hbmlmZXN0LXYxO3pEdlpSd3preHBna0hvUUxBck5raG16aWZ2WmY5RlVHTUc5QmlQamhpcVhGVW80VlR4ZGI7NTIwO2ZjZjMzYThkOTNlYzI3MWM2MGE3MDUxNmZhYTlkYzAzYmFhYjhiOWM5MTJiNzNkNjljOWNiZmYyMDNmNWVkY2MKb2JqZWN0PXBhbGFjZS0xO3BhbGFjZV9tYW5pZmVzdDthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLmNhdGFsb2ctbWFuaWZlc3QtdjE7ekR2WlJ3em03TE5ZQUM3WkxxUFVaQ05zQ0pTb0VHc1ZwU2hkRlJURWhLTWdadFE1eVZnUzszNjE7ZjE1MzUzZmFkOTZmYjEyYWY1MTRlMGYxN2U2ZDAyYTIxYjM4ZTE2YzQ1MWNjYzlhMWI3MDk4OTNjMDVjMTY2NgpjaGVja3N1bT1lYTJjMWNiODNjMThjNDg5MzBlMDcyODcxMTc4ZDk2YzUyZTY1NmU3NzFkMTRjZGMzMGY1NGVmNzhiYTJmNTU1Cg",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.TO3C1wME @ d80cf5b: mesh visibility ready, fetch still degraded verified=0
  Object.freeze({
    gitCommit: "d80cf5b9a1d46778f74320147b2f41e32022127d",
    snapshotNarHash:
      "sha256-gpnnwk46fKuVBc8DjfQmQ4kK452LFdi6kKtA+9J5W6M=",
    snapshotNarSize: 7282192,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "2eddeea8ad8c5c027951028441c9e3162776bb0eb4e2f8a877ec1d217bd57759",
    compiledReportSha256:
      "2c45dfe356422cf5ed633f026706eb678132e7b468681b2801c3c4799514fa5c",
    gate3ReportSha256:
      "bb654b78c8c925e790b6e713f172c44a33aac5fe7e8e43394ea0be1faf48622d",
    gate3Failure:
      "fetch exact MVP bundle on b degraded: state=degraded;published=8;verified=0;total=8;retention=degraded",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.12WxQ5A5 @ 6fddb8a: visibility ready; network-fetch-timeout on atrium
  Object.freeze({
    gitCommit: "6fddb8a45b160fd0d08c2cafefa9845d2d59c26b",
    snapshotNarHash:
      "sha256-8kknNzcFlRqTgKrMuTRQj9LCh6Aaz7vKyKaYDRFtNBg=",
    snapshotNarSize: 7284720,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "79a7b3b5ff307b11d8ea9313aa1bf3bfe097ce4a3c2b7945ede040ba6b91a095",
    compiledReportSha256:
      "75422dcf9b826b4cf098c76a96393f7e20445eac6cdad59fe95833b8571d71ad",
    gate3ReportSha256:
      "41295e02f3011b74ddd3f92922d315fc50231a659657fef09d7a8e3bba258b15",
    gate3Failure:
      "fetch exact MVP bundle on b degraded: state=degraded;published=8;verified=0;total=8;retention=degraded;failed_object=background-atrium;failed_reason=network-fetch-timeout",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.raitXnpX @ 0c4131b: fetch pre-cache still network-fetch-timeout
  Object.freeze({
    gitCommit: "0c4131b36fc7951fddebfd879330cd81b97c7869",
    snapshotNarHash:
      "sha256-JvX+N1pRo4KxhpE4vawyS9dV0ihYOozuUemDE8srbOc=",
    snapshotNarSize: 7286664,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "2e9c620b39e4a66e731055cf6302a64ffd5fb809461d0bfd67e59d1c9a766339",
    compiledReportSha256:
      "ef3f47b3bc4ba8f6ee8ed7b63994eb02d5804ecdf5f5e8459d9df877029a1b94",
    gate3ReportSha256:
      "2ee717436b85d15bd10ae77a55e2435f579983a17d10847fd72d265e36727c79",
    gate3Failure:
      "fetch exact MVP bundle on b degraded: state=degraded;published=8;verified=0;total=8;retention=degraded;failed_object=background-atrium;failed_reason=network-fetch-timeout",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.DZ7wkgNW @ 3d9a480: loopback mesh vis ready; still network-fetch-timeout
  Object.freeze({
    gitCommit: "3d9a4803ff3616d5fb8a6c2b0d4adac1651ad2ea",
    snapshotNarHash:
      "sha256-VL2LfzKb3Wkwnub5TlCd9lTRZMjnYVRLiVn5CZU0erU=",
    snapshotNarSize: 7288368,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "e6b6ae796408e9f71c487c6f22a167cbeb658eb4c21c17f84e791ba77857b5b1",
    compiledReportSha256:
      "bb65a2b1e7d52b435a5532fcd6e3cc78b02689f005b85a29c416e0e2889f0940",
    gate3ReportSha256:
      "5164dffc4a8b8bbdc9f2b38710c6713563ceffc2e13c4c3b303f1e32ed777493",
    gate3Failure:
      "fetch exact MVP bundle on b degraded: state=degraded;published=8;verified=0;total=8;retention=degraded;failed_object=background-atrium;failed_reason=network-fetch-timeout",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.M0t74b85 @ 0275e25: block copy without mark; still network-fetch-timeout
  Object.freeze({
    gitCommit: "0275e25e3530f3d98a0e9f5f68c12fa828f9e46e",
    snapshotNarHash:
      "sha256-SzwrFFGmBijgLQcG3XjNPJ82sOnkRyl5GpWKXNVbI50=",
    snapshotNarSize: 7292464,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "2199af80d1812df999ffa2c5cddbf22debaaf471f77906e029d12dc20b1a78d2",
    compiledReportSha256:
      "be62b92988c1d4aef08b592d1cdfb6aa96504766a6d9eea2eeb90fbfb2a8546d",
    gate3ReportSha256:
      "a3c300dc458b781fcd9fe9ccca66881bc47ff7ac28135dae51b8e4e2a1618329",
    gate3Failure:
      "fetch exact MVP bundle on b degraded: state=degraded;published=8;verified=0;total=8;retention=degraded;failed_object=background-atrium;failed_reason=network-fetch-timeout",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.IyK2mOBL @ ddd1db7: mark+local verify still hung downloadToUrlV2(local=
  // true) after cache source (native_available=8); complete from known bytes.
  Object.freeze({
    gitCommit: "ddd1db7f6cb20a028d1389b4f0729a56e7662939",
    snapshotNarHash:
      "sha256-SmUwa7IN6/dQA+I08/ySz46eamJYX7douWUWkwOufu4=",
    snapshotNarSize: 7295320,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "efac57a1b20ce950daca5640a716b31bc2161d476050be1ad2e9a6aa8a36be00",
    compiledReportSha256:
      "4bc0f2a1da72b0fce5ee557e6832b799616b4ce764bf169f4229da7b3ed4fab7",
    gate3ReportSha256:
      "fc7eb27b939634f3f86881b914aec5a7a759609492845223e397391c647fa083",
    gate3Failure:
      "fetch exact MVP bundle on b timed out after 600000 ms: state=fetching;published=8;verified=0;total=8;retention=missing;retention_round=0;source=cache;native_available=8;native_total=8;catalog=bG9nb3MtcGFsYWNlLW12cC1zdG9yYWdlLWNhdGFsb2ctdjEKdmVyc2lvbj0xCnJvb3Q9cGFsYWNlLTEKb2JqZWN0cz04Cm9iamVjdD1iYWNrZ3JvdW5kLWF0cml1bTtiYWNrZ3JvdW5kX3BuZztpbWFnZS9wbmc7ekR2WlJ3emt3UDNVdzRxd2RNNHJ6RXZOVFU4b0ExQlF1SndjTndxeE1LZ3JVdWJBU3k4ZDsxMTc2OTE2O2Y4MjdiNjVlZmVjZDVhNjI",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.BOnEa1w7 @ c513b3b: peer fetch verified=8 via known-bytes; retention
  // re-download hung downloadToUrlV2(local=true) on gate3VerifyRetention.
  Object.freeze({
    gitCommit: "c513b3bde84d15e46653647fb09d602e26a09ef9",
    snapshotNarHash:
      "sha256-66UxhjFWWd/Mf2LIHQJVRSYQUSxUmx12ecqZsWEXW14=",
    snapshotNarSize: 7305176,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "ec1a0ea9049ab083a5dab2e9d92834dc32ce5a048ddeaf7333f12a8a6fe931da",
    compiledReportSha256:
      "b0196d0e94c427fb940bdc5d754d69888cc9c73984ca44317547206a59129ee0",
    gate3ReportSha256:
      "102670a58a2be07021a00fba3948b00803ea801aa8696139867c453ea061f10a",
    gate3Failure:
      "worker b: gate3VerifyRetention receipt timeout: before=\"state=verified;published=8;verified=8;total=8;retention=missing;retention_round=0;source=cache;native_available=8;native_total=8;catalog=bG9nb3MtcGFsYWNlLW12cC1zdG9yYWdlLWNhdGFsb2ctdjEKdmVyc2lvbj0xCnJvb3Q9cGFsYWNlLTEKb2JqZWN0cz04Cm9iamVjdD1iYWNrZ3JvdW5kLWF0cml1bTtiYWNrZ3JvdW5kX3BuZztpbWFnZS9wbmc7ekR2WlJ3emt3UDNVdzRxd2RNNHJ6RXZOVFU4b0ExQlF1SndjTndxeE1LZ3JVdWJBU3k4ZDsxMTc2OTE2O2Y4MjdiNjVlZmVjZDVhNjIxMjU0ZGM4YjZjMmY0MTU0MWI2ZmI2MzI2Njc2YjVmYTFlZDNhODkyYjllN2JjMzcKb2JqZWN0PWJhY2tncm91bmQtbG91bmdlO2JhY2tncm91bmRfcG5nO2ltYWdlL3BuZzt6RHZaUnd6bTFRdFQyb0o2WTltZUp6YlNud3JpOWNLellUelVjd3RqWlJ6NDIyR2Via29ROzE2NzEzNjk7MDA1ZWRiYzY1NzA2Mjk3Y2VmYzZhYzZkZWVkZDc3ZTI5Zjc1NTE1OTI3MWIyZWM1YmI5OWI4YzlhODRjNjZmOApvYmplY3Q9cm9vbS1hdHJpdW0tbWV0YWRhdGE7cm9vbV9tZXRhZGF0YTthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLnJvb20tdjE7ekR2WlJ3em0zZk12R1NyZDF3S0xDenZ6aG1FOHhzdU5OeGFxbnBQQXg1WkFpc2g4dEsxaTsxOTU7MDlmMmI5MWE2MzE0NzBiNGRjNWI1ZDVkYzM4MTIwOTcwZjM5ZGQ4MDljMDQzYWFlYzhlN2IyNzBhZjA3NGMyYgpvYmplY3Q9cm9vbS1sb3VuZ2UtbWV0YWRhdGE7cm9vbV9tZXRhZGF0YTthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLnJvb20tdjE7ekR2WlJ3em1EOHN6M0xSRzhBMkVMVWplWjhkUVVLa21zeWJFcHEyajNkZFVQQnBIMjNKNzsxOTU7M2Q4MmJkNDRhYmNjZTAzMzg4ZjgyZTExMzA2OGM5N2E4ZjY5ZDE0Y2RhNDllOTQ4Zjg1ZjAzYWQ4ODcyM2RhNwpvYmplY3Q9c2NyaXB0LWRvb3I7c2NyaXB0X2J1bmRsZTthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLnNjcmlwdC12MTt6RHZaUnd6a3huTThtSm1KTVF4bjJjUXpZeTlrQ1B3clNtSnpIeHhXSENvSGFUcFUyV3VGOzQ3Ozk5YTRkMmQxNjIxYTUwMTRlYTk2Yzk2OTgxYzc4MzNkNGYwYTI4NTU5ZDU5YzBmNTdiN2UyMmYwMDdmNjUyNDUKb2JqZWN0PXJvb20tYXRyaXVtO3Jvb21fbWFuaWZlc3Q7YXBwbGljYXRpb24vdm5kLmxvZ29zLXBhbGFjZS5jYXRhbG9nLW1hbmlmZXN0LXYxO3pEdlpSd3ptQjF0WHQ0SHpBRVRHczEyY2hUZnpvWmU1ZVZYMkpMck1aS252YWJ1UFo2WFU7NTIwOzUyNGQzZmUzZTQ2MmI5ODMwZmI0NmU3MWY0MWEwOGZjNTVmMTVjNGE0N2ZmMDQwZWU5OGMwMGQ0MDdkNGQ0YTYKb2JqZWN0PXJvb20tbG91bmdlO3Jvb21fbWFuaWZlc3Q7YXBwbGljYXRpb24vdm5kLmxvZ29zLXBhbGFjZS5jYXRhbG9nLW1hbmlmZXN0LXYxO3pEdlpSd3preHBna0hvUUxBck5raG16aWZ2WmY5RlVHTUc5QmlQamhpcVhGVW80VlR4ZGI7NTIwO2ZjZjMzYThkOTNlYzI3MWM2MGE3MDUxNmZhYTlkYzAzYmFhYjhiOWM5MTJiNzNkNjljOWNiZmYyMDNmNWVkY2MKb2JqZWN0PXBhbGFjZS0xO3BhbGFjZV9tYW5pZmVzdDthcHBsaWNhdGlvbi92bmQubG9nb3MtcGFsYWNlLmNhdGFsb2ctbWFuaWZlc3QtdjE7ekR2WlJ3em03TE5ZQUM3WkxxUFVaQ05zQ0pTb0VHc1ZwU2hkRlJURWhLTWdadFE1eVZnUzszNjE7ZjE1MzUzZmFkOTZmYjEyYWY1MTRlMGYxN2U2ZDAyYTIxYjM4ZTE2YzQ1MWNjYzlhMWI3MDk4OTNjMDVjMTY2NgpjaGVja3N1bT1lYTJjMWNiODNjMThjNDg5MzBlMDcyODcxMTc4ZDk2YzUyZTY1NmU3NzFkMTRjZGMzMGY1NGVmNzhiYTJmNTU1Cg\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=45437;synced_height=45437;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=46->47",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile: identityRegistrationAndSealedMvpBundleProfile,
  }),
  // run.HA1Lqdln @ 1eb4e2c: B completed both retention rounds after A stopped,
  // then its exact verified PNG fetch timed out before cold-C work or any
  // Palace-root write.
  Object.freeze({
    gitCommit: "1eb4e2c76b3e3b0118ec78a8df7d214871c6568c",
    snapshotNarHash:
      "sha256-/o985IqfKf5YNc5/aUWsjjVmXyu1WWzIQSp5GCOyKtY=",
    snapshotNarSize: 7311024,
    snapshotRunnerSha256:
      "b09880ccfeee674e1c4388a06940c42084563405feaa854fdf0ae19b9fedaa55",
    runtimeManifestSha256:
      "12ae44fde819e24da2a891dadd40e539e45fbcd46b2af40f477bae430cb3cad2",
    compiledReportSha256:
      "dc9337cf66a506b0d8337a85d816ddd150660c44a2dcebea64d66e96c29f570d",
    gate3ReportSha256:
      "04a6a88dd9c90917f55e3dc1d01f09a775e64b5d7843838e84a5ecd9540046af",
    gate3Failure:
      "worker b: gate3FetchPng receipt timeout: before=\"missing\" after=\"\" state=\"wallet=created;ready=1;compatible=1;running=1;tracked=0;sync=current;current_height=45472;synced_height=45472;authority=missing;vm=idle;vm_action=none;program=e8ceab64ab3204d2309cc58c627478c98d39cda353fb3efa0d188ec5a4b25c61\" sequence=69->70",
    retirementStatus: "audited-pre-public-write-failure",
    reportProfile:
      identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOfflineProfile,
  }),
]);

function validPrePublicWriteAudit(audit) {
  const hasProfile = audit !== null
    && typeof audit === "object"
    && Object.hasOwn(audit, "reportProfile");
  return exactKeys(audit, [
      "compiledReportSha256",
      "gate3ReportSha256",
      "gate3Failure",
      "gitCommit",
      ...(hasProfile ? ["reportProfile"] : []),
      "retirementStatus",
      "runtimeManifestSha256",
    "snapshotNarHash",
    "snapshotNarSize",
    "snapshotRunnerSha256",
  ])
    && sourceCommitPattern.test(audit.gitCommit)
    && narHashPattern.test(audit.snapshotNarHash)
    && Number.isSafeInteger(audit.snapshotNarSize)
    && audit.snapshotNarSize > 0
    && sha256Pattern.test(audit.snapshotRunnerSha256)
    && sha256Pattern.test(audit.runtimeManifestSha256)
    && sha256Pattern.test(audit.compiledReportSha256)
    && sha256Pattern.test(audit.gate3ReportSha256)
    && typeof audit.gate3Failure === "string"
    && audit.gate3Failure.length > 0
    // Peer-fetch timeouts can embed the sealed catalog status string.
    && audit.gate3Failure.length <= 4096
    && (!hasProfile
      || [
        identityRegistrationAndIdleStorageProfile,
        identityRegistrationAndApprovalGuardedAssetsProfile,
        identityRegistrationAndPublishedAssetsProfile,
        identityRegistrationAndSealedMvpBundleProfile,
        identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOfflineProfile,
      ].includes(audit.reportProfile))
    && [
      "audited-fingerprint-rejection",
      "audited-pre-public-write-failure",
    ].includes(audit.retirementStatus);
}

function validPrePublicWriteAudits(audits) {
  if (
    !Array.isArray(audits)
    || audits.length === 0
    // Bound must cover long Gate 3 recovery histories (peer-fetch and
    // authoring iterations accumulate beyond a single dozen audits).
    || audits.length > 32
    || audits.some((audit) => !validPrePublicWriteAudit(audit))
  ) {
    return false;
  }
  const identities = audits.map((audit) => [
    audit.gitCommit,
    audit.snapshotNarHash,
    audit.snapshotNarSize,
    audit.snapshotRunnerSha256,
    audit.runtimeManifestSha256,
  ].join(":"));
  return new Set(identities).size === identities.length;
}

function exactKeys(value, expected) {
  return value !== null
    && typeof value === "object"
    && !Array.isArray(value)
    && JSON.stringify(Object.keys(value).sort())
      === JSON.stringify([...expected].sort());
}

function exactJson(left, right) {
  return JSON.stringify(left) === JSON.stringify(right);
}

export function sha256(bytes) {
  return createHash("sha256").update(bytes).digest("hex");
}

export function validateProcessScopeNames(
  processScopeSlice,
  processScopePrefix,
) {
  const match = /^logos-palace-run-([A-Za-z0-9]{8})\.slice$/.exec(
    processScopeSlice,
  );
  if (
    !match
    || processScopePrefix !== `logos-palace-run-${match[1]}`
  ) {
    throw new Error("active-run process scope names are invalid");
  }
  return { processScopeSlice, processScopePrefix };
}

function validateCommonIdentity(common) {
  if (
    !exactKeys(common, [
      "schema",
      "version",
      "uid",
      "releaseProgramId",
      "releaseRootId",
      "runDirectory",
      "productSnapshot",
      "gcRootPath",
      "gcRootTarget",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestPath",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ])
    || common.schema !== claimSchema
    || common.version !== 2
    || !Number.isSafeInteger(common.uid)
    || common.uid < 0
    || common.releaseProgramId !== releaseProgramId
    || common.releaseRootId !== releaseRootId
    || resolve(common.runDirectory) !== common.runDirectory
    || resolve(common.productSnapshot) !== common.productSnapshot
    || resolve(common.gcRootPath) !== common.gcRootPath
    || common.gcRootTarget !== common.productSnapshot
    || !sourceCommitPattern.test(common.gitCommit)
    || !narHashPattern.test(common.snapshotNarHash)
    || !Number.isSafeInteger(common.snapshotNarSize)
    || common.snapshotNarSize <= 0
    || common.snapshotNarSize > 64 * 1024 * 1024
    || !sha256Pattern.test(common.snapshotRunnerSha256)
    || resolve(common.runtimeManifestPath) !== common.runtimeManifestPath
    || !sha256Pattern.test(common.runtimeManifestSha256)
  ) {
    throw new Error("active-run common identity is invalid");
  }
  validateProcessScopeNames(
    common.processScopeSlice,
    common.processScopePrefix,
  );
  if (
    basename(common.runDirectory)
      !== `run.${common.processScopePrefix.slice(
        "logos-palace-run-".length,
      )}`
  ) {
    throw new Error("active-run directory and process scope identity differ");
  }
  return common;
}

function rollForwardKeys(value) {
  return exactKeys(value, [
    "evidence",
    "evidenceSha256",
    "retiredClaimArchive",
    "retiredClaimArchiveSha256",
    "predecessorClaimSha256",
    "predecessorCompiledReportSha256",
  ])
    && value.evidence === "claim-roll-forward.json"
    && sha256Pattern.test(value.evidenceSha256)
    && value.retiredClaimArchive === "retired-active-claim.json"
    && sha256Pattern.test(value.retiredClaimArchiveSha256)
    && sha256Pattern.test(value.predecessorClaimSha256)
    && sha256Pattern.test(value.predecessorCompiledReportSha256);
}

function commonFromClaim(claim) {
  return Object.fromEntries(
    [
      "schema",
      "version",
      "uid",
      "releaseProgramId",
      "releaseRootId",
      "runDirectory",
      "productSnapshot",
      "gcRootPath",
      "gcRootTarget",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestPath",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ].map((field) => [field, claim?.[field]]),
  );
}

export function validateStoredV2Claim(claim) {
  const common = validateCommonIdentity(commonFromClaim(claim));
  return validateV2Claim(claim, common);
}

export function validateStoredLegacyClaim(
  claim,
  legacyAudit = auditedLegacyPreGate3,
) {
  return validateLegacyClaim(claim, legacyAudit);
}

function matchesCommon(claim, expectedCommon) {
  return Object.entries(expectedCommon).every(
    ([field, value]) => claim?.[field] === value,
  );
}

export function validateV2Claim(claim, expectedCommon) {
  const stateFields = {
    "active-pre-gate3": [],
    "gate3-entered": ["gate3EnteredAtUnixMs"],
    completed: [
      "gate3EnteredAtUnixMs",
      "completedAtUnixMs",
      "compiledReportSha256",
    ],
  }[claim?.status];
  const optionalRollForward =
    Object.hasOwn(claim ?? {}, "rollForward") ? ["rollForward"] : [];
  if (
    !stateFields
    || !exactKeys(claim, [
      ...Object.keys(expectedCommon),
      "status",
      "createdAtUnixMs",
      ...stateFields,
      ...optionalRollForward,
    ])
    || !matchesCommon(claim, expectedCommon)
    || !Number.isSafeInteger(claim.createdAtUnixMs)
    || claim.createdAtUnixMs <= 0
    || (
      Object.hasOwn(claim, "rollForward")
      && !rollForwardKeys(claim.rollForward)
    )
    || (
      claim.status !== "active-pre-gate3"
      && (
        !Number.isSafeInteger(claim.gate3EnteredAtUnixMs)
        || claim.gate3EnteredAtUnixMs < claim.createdAtUnixMs
      )
    )
    || (
      claim.status === "completed"
      && (
        !Number.isSafeInteger(claim.completedAtUnixMs)
        || claim.completedAtUnixMs < claim.gate3EnteredAtUnixMs
        || !sha256Pattern.test(claim.compiledReportSha256)
      )
    )
  ) {
    throw new Error("persistent active-run claim v2 state is invalid");
  }
  return claim;
}

function transitionTimestamp(now, minimum) {
  const value = now();
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new Error("active-run transition timestamp is invalid");
  }
  return Math.max(value, minimum);
}

function validateLegacyClaim(claim, legacyAudit) {
  if (
    !exactKeys(claim, [
      "schema",
      "version",
      "uid",
      "releaseProgramId",
      "releaseRootId",
      "runDirectory",
      "productSnapshot",
      "gcRootPath",
      "gcRootTarget",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestPath",
      "runtimeManifestSha256",
      "status",
      "createdAtUnixMs",
    ])
    || claim.schema !== claimSchema
    || claim.version !== 1
    || claim.releaseProgramId !== releaseProgramId
    || claim.releaseRootId !== releaseRootId
    || claim.status !== "active"
    || !Number.isSafeInteger(claim.uid)
    || claim.uid < 0
    || resolve(claim.runDirectory) !== claim.runDirectory
    || resolve(claim.productSnapshot) !== claim.productSnapshot
    || resolve(claim.gcRootPath) !== claim.gcRootPath
    || claim.gcRootTarget !== claim.productSnapshot
    || resolve(claim.runtimeManifestPath) !== claim.runtimeManifestPath
    || !Number.isSafeInteger(claim.createdAtUnixMs)
    || claim.createdAtUnixMs <= 0
    || claim.gitCommit !== legacyAudit.gitCommit
    || claim.snapshotNarHash !== legacyAudit.snapshotNarHash
    || claim.snapshotNarSize !== legacyAudit.snapshotNarSize
    || claim.snapshotRunnerSha256
      !== legacyAudit.snapshotRunnerSha256
    || claim.runtimeManifestSha256
      !== legacyAudit.runtimeManifestSha256
  ) {
    throw new Error("legacy active-run claim is not the audited predecessor");
  }
  return claim;
}

async function canonicalOwnerDirectory(path, uid, mode) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isDirectory()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== mode
    || await realpath(path) !== path
  ) {
    throw new Error("claim lifecycle directory is not canonical owner state");
  }
  return path;
}

async function secureFile(path, uid, maximum, description) {
  const metadata = await lstat(path);
  if (
    metadata.isSymbolicLink()
    || !metadata.isFile()
    || metadata.uid !== uid
    || (metadata.mode & 0o777) !== 0o600
    || metadata.size <= 0
    || metadata.size > maximum
    || await realpath(path) !== path
  ) {
    throw new Error(`${description} is not a secure regular file`);
  }
  const bytes = await readFile(path);
  if (bytes.length !== metadata.size) {
    throw new Error(`${description} changed while being read`);
  }
  return bytes;
}

async function parseSecureJson(path, uid, maximum, description) {
  const bytes = await secureFile(path, uid, maximum, description);
  let value;
  try {
    value = JSON.parse(bytes);
  } catch {
    throw new Error(`${description} is not valid JSON`);
  }
  return { bytes, value, sha256: sha256(bytes) };
}

async function absent(path, description) {
  try {
    await lstat(path);
  } catch (error) {
    if (error?.code === "ENOENT") return;
    throw error;
  }
  throw new Error(`${description} must be absent`);
}

async function syncDirectory(path) {
  const handle = await open(path, "r");
  try {
    await handle.sync();
  } finally {
    await handle.close();
  }
}

async function writeExclusiveDurable(path, bytes, uid) {
  const directory = dirname(path);
  const temporary = join(
    directory,
    `.${basename(path)}-${process.pid}-${Date.now()}.tmp`,
  );
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(bytes);
    await handle.sync();
    await handle.close();
    handle = undefined;
    await link(temporary, path);
    await unlink(temporary);
    await syncDirectory(directory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    if (error?.code !== "EEXIST") throw error;
    const existing = await secureFile(
      path,
      uid,
      128 * 1024,
      "existing claim lifecycle evidence",
    );
    if (!existing.equals(bytes)) {
      throw new Error("existing claim lifecycle evidence differs");
    }
  }
}

async function writeNewClaim(claimDirectory, claimPath, value) {
  const temporary = join(
    claimDirectory,
    `.claim-${process.pid}-${Date.now()}.tmp`,
  );
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await link(temporary, claimPath);
    await unlink(temporary);
    await syncDirectory(claimDirectory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
}

async function durableReplaceClaim(claimDirectory, claimPath, value) {
  const temporary = join(
    claimDirectory,
    `.claim-${process.pid}-${Date.now()}.tmp`,
  );
  let handle;
  try {
    handle = await open(temporary, "wx", 0o600);
    await handle.writeFile(`${JSON.stringify(value, null, 2)}\n`, "utf8");
    await handle.sync();
    await handle.close();
    handle = undefined;
    await rename(temporary, claimPath);
    await syncDirectory(claimDirectory);
  } catch (error) {
    await handle?.close().catch(() => {});
    await unlink(temporary).catch((unlinkError) => {
      if (unlinkError?.code !== "ENOENT") throw unlinkError;
    });
    throw error;
  }
}

async function readClaim(claimPath, uid) {
  try {
    return await parseSecureJson(
      claimPath,
      uid,
      64 * 1024,
      "persistent active-run claim",
    );
  } catch (error) {
    if (error?.code === "ENOENT") return undefined;
    throw error;
  }
}

function validateFailedReport(
  report,
  predecessor,
  allowedFailurePhases,
  description,
) {
  const expectedGateReports = {
    gate0: null,
    gate1: "gate1/gate1-report.json",
    gate2: "gate2/gate2-report.json",
    gate3: "gate3/gate3-report.json",
    gate4: "gate4/gate4-report.json",
    gate5: "gate4/gate4-report.json",
    gate6: "gate4/gate4-report.json",
  };
  if (
    !exactKeys(report, [
      "schema",
      "version",
      "status",
      "fullMvp",
      "productSnapshot",
      "scope",
      "failure",
      "gates",
    ])
    || report.schema !== reportSchema
    || report.version !== 1
    || report.status !== "failed"
    || report.fullMvp !== "not-evaluated"
    || report.productSnapshot !== predecessor.productSnapshot
    || !exactKeys(report.scope, ["implementedGates", "pendingGates"])
    || !exactJson(report.scope.implementedGates, implementedGates)
    || !exactJson(report.scope.pendingGates, [])
    || !exactKeys(report.failure, ["phase", "message"])
    || !allowedFailurePhases.has(report.failure.phase)
    || typeof report.failure.message !== "string"
    || report.failure.message.length <= 0
    || report.failure.message.length > 1024
    || !exactKeys(report.gates, implementedGates)
  ) {
    throw new Error(`${description} failed compiled report is invalid`);
  }
  for (const gate of implementedGates) {
    if (
      !exactKeys(report.gates[gate], ["status", "report"])
      || report.gates[gate].status !== "unknown"
      || report.gates[gate].report !== expectedGateReports[gate]
    ) {
      throw new Error(`${description} failed compiled gate state is invalid`);
    }
  }
  return report.failure.phase;
}

function validatePreGate3FailedReport(report, predecessor) {
  return validateFailedReport(
    report,
    predecessor,
    preGate3Phases,
    "pre-Gate 3",
  );
}

function validateAuditedGate3FailedReport(report, predecessor) {
  return validateFailedReport(
    report,
    predecessor,
    new Set(["gate3"]),
    "audited Gate 3",
  );
}

async function validatePredecessorIdentity({
  predecessor,
  common,
  uid,
  validateImmutableSnapshot,
}) {
  if (
    predecessor.uid !== uid
    || predecessor.runDirectory === common.runDirectory
    || dirname(predecessor.runDirectory) !== dirname(common.runDirectory)
  ) {
    throw new Error("predecessor run ownership or scope differs");
  }
  await canonicalOwnerDirectory(predecessor.runDirectory, uid, 0o700);
  await validateImmutableSnapshot(predecessor);

  const marker = await secureFile(
    join(predecessor.runDirectory, "product-snapshot"),
    uid,
    4096,
    "predecessor product snapshot marker",
  );
  if (marker.toString("utf8") !== `${predecessor.productSnapshot}\n`) {
    throw new Error("predecessor product snapshot marker differs");
  }

  const identity = await parseSecureJson(
    join(predecessor.runDirectory, "source-identity.json"),
    uid,
    64 * 1024,
    "predecessor source identity",
  );
  if (
    !exactKeys(identity.value, [
      "schema",
      "version",
      "gitCommit",
      "productSnapshot",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "trackedPathsSha256",
      "snapshotEvidenceSha256",
      "snapshotGcRoot",
    ])
    || identity.value.schema !== "logos.palace.basecamp-source-identity"
    || identity.value?.version !== 1
    || identity.value?.gitCommit !== predecessor.gitCommit
    || identity.value?.productSnapshot !== predecessor.productSnapshot
    || identity.value?.snapshotGcRoot !== predecessor.gcRootPath
    || identity.value?.snapshotNarHash !== predecessor.snapshotNarHash
    || identity.value?.snapshotNarSize !== predecessor.snapshotNarSize
    || identity.value?.snapshotRunnerSha256
      !== predecessor.snapshotRunnerSha256
    || !sha256Pattern.test(identity.value?.trackedPathsSha256)
    || !sha256Pattern.test(identity.value?.snapshotEvidenceSha256)
  ) {
    throw new Error("predecessor source identity differs");
  }

  const runtimeManifest = await secureFile(
    predecessor.runtimeManifestPath,
    uid,
    128 * 1024,
    "predecessor runtime manifest",
  );
  if (
    dirname(predecessor.runtimeManifestPath) !== predecessor.runDirectory
    || basename(predecessor.runtimeManifestPath)
      !== "runtime-output-manifest.json"
    || sha256(runtimeManifest) !== predecessor.runtimeManifestSha256
  ) {
    throw new Error("predecessor runtime manifest differs");
  }

  const gcRoot = await lstat(predecessor.gcRootPath);
  if (
    !gcRoot.isSymbolicLink()
    || await realpath(predecessor.gcRootPath) !== predecessor.productSnapshot
  ) {
    throw new Error("predecessor GC root does not retain its snapshot");
  }
}

async function validatePreGate3Artifacts({
  predecessor,
  claimVersion,
  uid,
  legacyAudit,
}) {
  const run = predecessor.runDirectory;
  await absent(join(run, "gate3"), "predecessor Gate 3 evidence");
  await absent(join(run, "gate4"), "predecessor Gate 4 evidence");
  await absent(
    join(run, "active-claim-completion.json"),
    "predecessor claim completion",
  );
  await absent(
    join(run, "public-evidence.json"),
    "predecessor public evidence",
  );
  const sharedState = join(run, "shared-state");
  await canonicalOwnerDirectory(sharedState, uid, 0o700);
  if ((await readdir(sharedState)).length !== 0) {
    throw new Error("predecessor shared state is not empty");
  }

  const compiled = await parseSecureJson(
    join(run, "compiled-mvp-report.json"),
    uid,
    4 * 1024 * 1024,
    "predecessor compiled report",
  );
  const failurePhase = validatePreGate3FailedReport(
    compiled.value,
    predecessor,
  );

  if (claimVersion === 1) {
    if (
      failurePhase !== "gate1"
      || compiled.sha256 !== legacyAudit.compiledReportSha256
    ) {
      throw new Error("legacy failed report is not the audited report");
    }
    const gate0 = await secureFile(
      join(run, "gate0", "gate0-report.json"),
      uid,
      4 * 1024 * 1024,
      "legacy Gate 0 report",
    );
    const gate1 = await secureFile(
      join(run, "gate1", "gate1-report.json"),
      uid,
      4 * 1024 * 1024,
      "legacy Gate 1 report",
    );
    if (
      sha256(gate0) !== legacyAudit.gate0ReportSha256
      || sha256(gate1) !== legacyAudit.gate1ReportSha256
    ) {
      throw new Error("legacy gate reports are not the audited reports");
    }
    await absent(join(run, "gate2"), "legacy Gate 2 evidence");
  } else {
    const laterGates = {
      gate0: ["gate1", "gate2"],
      gate1: ["gate2"],
      gate2: [],
    }[failurePhase] ?? [];
    for (const gate of laterGates) {
      await absent(join(run, gate), `predecessor ${gate} evidence`);
    }
  }
  return {
    compiledReportSha256: compiled.sha256,
    failurePhase,
  };
}

function matchesAuditedPrePublicWriteFailure(predecessor, audit) {
  return predecessor.version === 2
    && predecessor.gitCommit === audit.gitCommit
    && predecessor.snapshotNarHash === audit.snapshotNarHash
    && predecessor.snapshotNarSize === audit.snapshotNarSize
    && predecessor.snapshotRunnerSha256 === audit.snapshotRunnerSha256
    && predecessor.runtimeManifestSha256 === audit.runtimeManifestSha256;
}

function receiptFields(receipt) {
  const fields = Object.create(null);
  for (const field of String(receipt).split(";")) {
    const separator = field.indexOf("=");
    if (separator > 0) {
      fields[field.slice(0, separator)] = field.slice(separator + 1);
    }
  }
  return fields;
}

function validIdentityRegistration(record, display) {
  if (
    !exactKeys(record, [
      "accountId",
      "deliveryKey",
      "display",
      "existing",
      "keyEpoch",
      "receipt",
      "registrationTransaction",
    ])
    || record.display !== display
    || typeof record.existing !== "boolean"
    || record.keyEpoch !== "1"
    || !sha256Pattern.test(record.accountId)
    || !sha256Pattern.test(record.deliveryKey)
    || !sha256Pattern.test(record.registrationTransaction)
  ) {
    return false;
  }
  const fields = receiptFields(record.receipt);
  return fields.identity === record.accountId
    && fields.display === display
    && fields.delivery_key === record.deliveryKey
    && fields.key_epoch === "1"
    && fields.registration === "submitted"
    && fields.registration_ready === "1"
    && fields.registration_tx === record.registrationTransaction;
}

function validCurrentLezAuthorityFields(fields) {
  return fields.ready === "1"
    && fields.compatible === "1"
    && fields.running === "1"
    && fields.tracked === "0"
    && fields.sync === "current"
    && fields.current_height === fields.synced_height
    && /^[1-9][0-9]*$/.test(fields.current_height ?? "")
    && fields.authority === "missing"
    && fields.vm === "idle"
    && fields.vm_action === "none"
    && fields.program === releaseProgramId;
}

function validCurrentNoAuthorityLez(startup) {
  if (
    !exactKeys(startup, ["basecampPid", "lez", "startupMs"])
    || !Number.isSafeInteger(startup.basecampPid)
    || startup.basecampPid <= 1
    || !Number.isSafeInteger(startup.startupMs)
    || startup.startupMs < 0
    || !Number.isSafeInteger(startup.lez?.elapsedMs)
    || startup.lez.elapsedMs < 0
  ) {
    return false;
  }

  // Observed through gate4LezState when gate4StartLez returns an empty action
  // receipt and the durable UI state is already current.
  if (
    exactKeys(startup.lez, ["elapsedMs", "lezStateObservation", "receipt"])
    && startup.lez.receipt === ""
    && exactKeys(startup.lez.lezStateObservation, ["receipt", "source"])
    && startup.lez.lezStateObservation.source === "gate4LezState"
  ) {
    return validCurrentLezAuthorityFields(
      receiptFields(startup.lez.lezStateObservation.receipt),
    );
  }

  // Direct terminal receipt when gate4StartLez returns ok;... with current state.
  if (exactKeys(startup.lez, ["elapsedMs", "receipt"])) {
    return String(startup.lez.receipt).startsWith("ok;")
      && validCurrentLezAuthorityFields(receiptFields(startup.lez.receipt));
  }

  return false;
}

function validIdleStorageStatus(status, expectedState) {
  if (
    !exactKeys(status, ["elapsedMs", "receipt"])
    || !Number.isSafeInteger(status.elapsedMs)
    || status.elapsedMs < 0
  ) {
    return false;
  }
  const fields = receiptFields(status.receipt);
  return fields.storage === expectedState
    && fields.pending === "0"
    && fields.callbacks === "0"
    && fields.callback_registration === "ready"
    && fields.reconciliation_required === "0"
    && fields.catalog === "idle"
    && fields.catalog_verified === "0"
    && fields.retention_round === "0"
    && fields.retained === "0";
}

function validIdentityRegistrationAndIdleStorageAssetAuthoring(authoring) {
  return exactKeys(authoring, [
    "assets",
    "boundary",
    "elapsedMs",
    "graphBindings",
    "inputManifest",
    "phase",
    "propStory",
    "selectedAssetCount",
    "version",
  ])
    && authoring.version === 1
    && authoring.phase === "input-validated"
    && Number.isSafeInteger(authoring.selectedAssetCount)
    && authoring.selectedAssetCount > 0
    && typeof authoring.propStory === "string"
    && typeof authoring.boundary === "string"
    && authoring.elapsedMs === 0
    && exactJson(authoring.assets, [])
    && exactJson(authoring.graphBindings, [])
    && exactKeys(authoring.inputManifest, [
      "assetCount",
      "schema",
      "sha256",
      "version",
    ])
    && authoring.inputManifest.schema === "logos.palace.e2e-asset-inputs"
    && authoring.inputManifest.version === 1
    && Number.isSafeInteger(authoring.inputManifest.assetCount)
    && authoring.inputManifest.assetCount > 0
    && sha256Pattern.test(authoring.inputManifest.sha256);
}

function validApprovalGuardedStagedAsset(asset) {
  if (
    !exactKeys(asset, [
      "appends",
      "assetId",
      "begin",
      "byteLength",
      "chunkBytes",
      "chunkCount",
      "commit",
      "file",
      "handle",
      "height",
      "label",
      "role",
      "target",
      "width",
    ])
    || !/^[a-z0-9][a-z0-9-]{0,127}$/.test(asset.assetId)
    || !sha256Pattern.test(asset.handle)
    || asset.role !== "room-background"
    || !exactKeys(asset.target, ["kind", "roomId"])
    || asset.target.kind !== "room-background"
    || !/^[a-z0-9][a-z0-9-]{0,63}$/.test(asset.target.roomId)
    || typeof asset.file !== "string"
    || asset.file.length === 0
    || asset.file.length > 256
    || asset.file !== asset.label
    || !asset.file.endsWith(".png")
    || /[\\/]/.test(asset.file)
    || !Number.isSafeInteger(asset.byteLength)
    || asset.byteLength <= 0
    || asset.byteLength > 10 * 1024 * 1024
    || asset.chunkBytes !== 32 * 1024
    || !Number.isSafeInteger(asset.chunkCount)
    || asset.chunkCount !== Math.ceil(asset.byteLength / asset.chunkBytes)
    || !Number.isSafeInteger(asset.width)
    || asset.width <= 0
    || asset.width > 16_384
    || !Number.isSafeInteger(asset.height)
    || asset.height <= 0
    || asset.height > 16_384
    || !exactKeys(asset.begin, ["elapsedMs", "receipt"])
    || !Number.isSafeInteger(asset.begin.elapsedMs)
    || asset.begin.elapsedMs < 0
    || typeof asset.begin.receipt !== "string"
    || !asset.begin.receipt.startsWith("ok;")
    || !Array.isArray(asset.appends)
    || asset.appends.length !== asset.chunkCount
    || !exactKeys(asset.commit, ["elapsedMs", "receipt"])
    || !Number.isSafeInteger(asset.commit.elapsedMs)
    || asset.commit.elapsedMs < 0
    || typeof asset.commit.receipt !== "string"
    || !asset.commit.receipt.startsWith("ok;")
  ) {
    return false;
  }

  const begin = receiptFields(asset.begin.receipt);
  const maxTotalBytes = Number(begin.maxTotalBytes);
  if (
    !exactKeys(begin, ["maxChunkBytes", "maxTotalBytes", "next", "session"])
    || !/^[a-f0-9]{32}$/.test(begin.session)
    || begin.next !== "0"
    || begin.maxChunkBytes !== String(asset.chunkBytes)
    || !/^[1-9][0-9]*$/.test(begin.maxTotalBytes)
    || !Number.isSafeInteger(maxTotalBytes)
    || maxTotalBytes < asset.byteLength
  ) {
    return false;
  }

  let totalBytes = 0;
  for (const [sequence, append] of asset.appends.entries()) {
    if (
      !exactKeys(append, ["byteLength", "elapsedMs", "receipt", "sequence"])
      || append.sequence !== sequence
      || !Number.isSafeInteger(append.byteLength)
      || append.byteLength <= 0
      || append.byteLength > asset.chunkBytes
      || !Number.isSafeInteger(append.elapsedMs)
      || append.elapsedMs < 0
      || typeof append.receipt !== "string"
      || !append.receipt.startsWith("ok;")
    ) {
      return false;
    }
    totalBytes += append.byteLength;
    const fields = receiptFields(append.receipt);
    if (
      !exactKeys(fields, ["bytes", "next", "session"])
      || fields.session !== begin.session
      || fields.next !== String(sequence + 1)
      || fields.bytes !== String(totalBytes)
    ) {
      return false;
    }
  }
  if (totalBytes !== asset.byteLength) return false;

  const commit = receiptFields(asset.commit.receipt);
  return exactKeys(commit, ["bytes", "handle", "height", "width"])
    && commit.handle === asset.handle
    && commit.width === String(asset.width)
    && commit.height === String(asset.height)
    && commit.bytes === String(asset.byteLength);
}

function validIdentityRegistrationAndApprovalGuardedAssets(authoring) {
  if (
    !exactKeys(authoring, [
      "assets",
      "boundary",
      "elapsedMs",
      "graphBindings",
      "guardedBeforeApproval",
      "inputManifest",
      "phase",
      "propStory",
      "selectedAssetCount",
      "version",
    ])
    || authoring.version !== 1
    || authoring.phase !== "approval-guarded"
    || authoring.propStory !== "not-requested"
    || typeof authoring.boundary !== "string"
    || authoring.boundary.length === 0
    || !Number.isSafeInteger(authoring.elapsedMs)
    || authoring.elapsedMs < 0
    || !Number.isSafeInteger(authoring.selectedAssetCount)
    || authoring.selectedAssetCount <= 0
    || !Array.isArray(authoring.assets)
    || authoring.assets.length !== authoring.selectedAssetCount
    || !exactJson(authoring.graphBindings, [])
    || !exactKeys(authoring.guardedBeforeApproval, ["elapsedMs", "receipt"])
    || authoring.guardedBeforeApproval.receipt !== "rejected=asset-not-approved"
    || !Number.isSafeInteger(authoring.guardedBeforeApproval.elapsedMs)
    || authoring.guardedBeforeApproval.elapsedMs < 0
    || !exactKeys(authoring.inputManifest, [
      "assetCount",
      "schema",
      "sha256",
      "version",
    ])
    || authoring.inputManifest.schema !== "logos.palace.e2e-asset-inputs"
    || authoring.inputManifest.version !== 1
    || authoring.inputManifest.assetCount !== authoring.selectedAssetCount
    || !sha256Pattern.test(authoring.inputManifest.sha256)
    || !authoring.assets.every(validApprovalGuardedStagedAsset)
  ) {
    return false;
  }
  const assetIds = authoring.assets.map((asset) => asset.assetId);
  const files = authoring.assets.map((asset) => asset.file);
  const handles = authoring.assets.map((asset) => asset.handle);
  return new Set(assetIds).size === assetIds.length
    && new Set(files).size === files.length
    && new Set(handles).size === handles.length;
}

function validIdentityRegistrationAndIdleStorageBase(report) {
  return exactKeys(report.identities, ["a", "b", "c"])
    && validIdentityRegistration(report.identities.a, "Alice")
    && validIdentityRegistration(report.identities.b, "Bob")
    && validIdentityRegistration(report.identities.c, "Carol")
    && exactKeys(report.startup, ["a", "b", "c"])
    && validCurrentNoAuthorityLez(report.startup.a)
    && validCurrentNoAuthorityLez(report.startup.b)
    && validCurrentNoAuthorityLez(report.startup.c)
    && exactKeys(report.storageConfigs, ["a", "b", "c"])
    && Object.values(report.storageConfigs).every(
      (config) => typeof config === "string" && config.length > 0,
    )
    && exactKeys(report.storageStartup, ["a", "b"])
    && ["a", "b"].every((label) =>
      exactKeys(report.storageStartup[label], ["running", "start"])
      && validIdleStorageStatus(report.storageStartup[label].start, "starting")
      && validIdleStorageStatus(report.storageStartup[label].running, "running"),
    );
}

function validIdentityRegistrationAndIdleStorageGate3Report(report) {
  return validIdentityRegistrationAndIdleStorageBase(report)
    && validIdentityRegistrationAndIdleStorageAssetAuthoring(report.assetAuthoring);
}

function validIdentityRegistrationAndApprovalGuardedAssetsGate3Report(report) {
  return validIdentityRegistrationAndIdleStorageBase(report)
    && validIdentityRegistrationAndApprovalGuardedAssets(report.assetAuthoring);
}

function validPublishedAssetInvocation(stage, { allowPublishedPrefix = false } = {}) {
  return exactKeys(stage, ["elapsedMs", "receipt"])
    && Number.isSafeInteger(stage.elapsedMs)
    && stage.elapsedMs >= 0
    && typeof stage.receipt === "string"
    && (
      stage.receipt.startsWith("ok;")
      || (
        allowPublishedPrefix
        && stage.receipt.startsWith("published;cid=")
      )
    );
}

function validPublishedAuthoringAsset(asset) {
  // JSON checkpointing omits `assignment: undefined`, so unpublished
  // assignments are either absent or explicitly null.
  const hasAssignmentKey = Object.hasOwn(asset, "assignment");
  if (
    !validApprovalGuardedStagedAsset({
      appends: asset.appends,
      assetId: asset.assetId,
      begin: asset.begin,
      byteLength: asset.byteLength,
      chunkBytes: asset.chunkBytes,
      chunkCount: asset.chunkCount,
      commit: asset.commit,
      file: asset.file,
      handle: asset.handle,
      height: asset.height,
      label: asset.label,
      role: asset.role,
      target: asset.target,
      width: asset.width,
    })
    || !exactKeys(asset, [
      "appends",
      "assetId",
      ...(hasAssignmentKey ? ["assignment"] : []),
      "begin",
      "byteLength",
      "chunkBytes",
      "chunkCount",
      "cid",
      "commit",
      "file",
      "handle",
      "height",
      "label",
      "publication",
      "review",
      "role",
      "target",
      "width",
    ])
    || !storageCidPattern.test(asset.cid)
    || !validPublishedAssetInvocation(asset.review)
    || !asset.review.receipt.startsWith(`ok;handle=${asset.handle};review=approved`)
    || !exactKeys(asset.publication, ["completed", "dispatched"])
    || !validPublishedAssetInvocation(asset.publication.dispatched)
    || asset.publication.dispatched.receipt !== "ok;asset=publishing"
    || !validPublishedAssetInvocation(
      asset.publication.completed,
      { allowPublishedPrefix: true },
    )
    || asset.publication.completed.receipt !== `published;cid=${asset.cid}`
  ) {
    return false;
  }
  if (!hasAssignmentKey || asset.assignment === null) return true;
  if (
    !validPublishedAssetInvocation(asset.assignment)
    || !/^ok;room=(atrium|lounge);handle=[0-9a-f]{64}$/.test(
      asset.assignment.receipt,
    )
  ) {
    return false;
  }
  const fields = receiptFields(asset.assignment.receipt);
  return fields.handle === asset.handle
    && ["atrium", "lounge"].includes(fields.room);
}

function authoringAssetIsAssigned(asset) {
  return Object.hasOwn(asset, "assignment")
    && asset.assignment !== null
    && asset.assignment !== undefined;
}

function validIdentityRegistrationAndPublishedAssets(authoring) {
  // Phase may advance to "complete" after final room assignment receipts are
  // checkpointed, then still fail closed before the admin screenshot / graph
  // publish path (still pre-public-write when graphBindings stay empty).
  const hasCatalogCount = Object.hasOwn(authoring, "catalogCount");
  const hasAssignments = Object.hasOwn(authoring, "assignments");
  if (
    !exactKeys(authoring, [
      "assets",
      ...(hasAssignments ? ["assignments"] : []),
      "boundary",
      ...(hasCatalogCount ? ["catalogCount"] : []),
      "elapsedMs",
      "graphBindings",
      "guardedBeforeApproval",
      "inputManifest",
      "phase",
      "propStory",
      "selectedAssetCount",
      "version",
    ])
    || authoring.version !== 1
    || !["published", "complete"].includes(authoring.phase)
    || authoring.propStory !== "not-requested"
    || typeof authoring.boundary !== "string"
    || authoring.boundary.length === 0
    || !Number.isSafeInteger(authoring.elapsedMs)
    || authoring.elapsedMs < 0
    || !Number.isSafeInteger(authoring.selectedAssetCount)
    || authoring.selectedAssetCount <= 0
    || !Array.isArray(authoring.assets)
    || authoring.assets.length !== authoring.selectedAssetCount
    || !exactJson(authoring.graphBindings, [])
    || !exactKeys(authoring.guardedBeforeApproval, ["elapsedMs", "receipt"])
    || authoring.guardedBeforeApproval.receipt !== "rejected=asset-not-approved"
    || !Number.isSafeInteger(authoring.guardedBeforeApproval.elapsedMs)
    || authoring.guardedBeforeApproval.elapsedMs < 0
    || !exactKeys(authoring.inputManifest, [
      "assetCount",
      "schema",
      "sha256",
      "version",
    ])
    || authoring.inputManifest.schema !== "logos.palace.e2e-asset-inputs"
    || authoring.inputManifest.version !== 1
    || authoring.inputManifest.assetCount !== authoring.selectedAssetCount
    || !sha256Pattern.test(authoring.inputManifest.sha256)
    || !authoring.assets.every(validPublishedAuthoringAsset)
    || (
      hasCatalogCount
      && (
        !Number.isSafeInteger(authoring.catalogCount)
        || authoring.catalogCount !== authoring.selectedAssetCount
      )
    )
    || (
      hasAssignments
      && (
        !exactKeys(authoring.assignments, ["prop", "rooms"])
        || authoring.assignments.prop !== null
        || !exactKeys(authoring.assignments.rooms, ["atrium", "lounge"])
        || !sha256Pattern.test(authoring.assignments.rooms.atrium)
        || !sha256Pattern.test(authoring.assignments.rooms.lounge)
      )
    )
  ) {
    return false;
  }
  const assetIds = authoring.assets.map((asset) => asset.assetId);
  const files = authoring.assets.map((asset) => asset.file);
  const handles = authoring.assets.map((asset) => asset.handle);
  const cids = authoring.assets.map((asset) => asset.cid);
  return new Set(assetIds).size === assetIds.length
    && new Set(files).size === files.length
    && new Set(handles).size === handles.length
    && new Set(cids).size === cids.length
    && authoring.assets.some((asset) => authoringAssetIsAssigned(asset));
}

function validIdentityRegistrationAndPublishedAssetsGate3Report(report) {
  return validIdentityRegistrationAndIdleStorageBase(report)
    && validIdentityRegistrationAndPublishedAssets(report.assetAuthoring);
}

function validSealedMvpBundlePublication(publication) {
  if (
    !exactKeys(publication, ["checksum", "completed", "dispatched", "objects"])
    || !validPublishedAssetInvocation(publication.dispatched)
    || !String(publication.dispatched.receipt).startsWith("ok;")
    || !exactKeys(publication.completed, ["elapsedMs", "receipt"])
    || !Number.isSafeInteger(publication.completed.elapsedMs)
    || publication.completed.elapsedMs < 0
    || typeof publication.completed.receipt !== "string"
    || !publication.completed.receipt.includes("state=verified")
    || !publication.completed.receipt.includes("catalog=")
    || typeof publication.checksum !== "string"
    || !sha256Pattern.test(publication.checksum)
    || !Array.isArray(publication.objects)
    || publication.objects.length < 5
  ) {
    return false;
  }
  return publication.objects.every((object) =>
    exactKeys(object, [
      "byteLength",
      "cid",
      "contentSha256",
      "mediaType",
      "objectId",
      "type",
    ])
      && typeof object.objectId === "string"
      && object.objectId.length > 0
      && storageCidPattern.test(object.cid)
      && sha256Pattern.test(object.contentSha256)
      && Number.isSafeInteger(object.byteLength)
      && object.byteLength > 0
  );
}

function validSealedMvpBundleGraphBindings(authoring, publication) {
  if (
    !Array.isArray(authoring.graphBindings)
    || authoring.graphBindings.length < 2
    || !Array.isArray(publication.objects)
  ) {
    return false;
  }
  const objectsById = new Map(
    publication.objects.map((object) => [object.objectId, object]),
  );
  return authoring.graphBindings.every((binding) => {
    const expectedKeys = binding?.kind === "room-background"
      ? [
          "kind",
          "targetId",
          "objectId",
          "assetId",
          "assignment",
          "cid",
          "contentSha256",
        ]
      : [
          "kind",
          "objectId",
          "assetId",
          "assignment",
          "cid",
          "contentSha256",
        ];
    if (!exactKeys(binding, expectedKeys) || !storageCidPattern.test(binding.cid)) {
      return false;
    }
    const object = objectsById.get(binding.objectId);
    const asset = authoring.assets.find(
      (candidate) => candidate.assetId === binding.assetId,
    );
    return object !== undefined
      && asset !== undefined
      && object.cid === binding.cid
      && object.contentSha256 === binding.contentSha256
      && asset.cid === binding.cid
      && asset.handle === binding.contentSha256;
  });
}

function validIdentityRegistrationAndSealedMvpBundle(authoring, publication) {
  // Sealed-bundle reports add graphBindings, optional activePropProjection, and
  // a top-level publication object after creator publish succeeds.
  const {
    activePropProjection,
    graphBindings,
    ...publishedShape
  } = authoring;
  if (
    !validIdentityRegistrationAndPublishedAssets({
      ...publishedShape,
      graphBindings: [],
    })
    || !validSealedMvpBundlePublication(publication)
    || !validSealedMvpBundleGraphBindings(authoring, publication)
  ) {
    return false;
  }
  if (activePropProjection !== undefined) {
    if (
      !exactKeys(activePropProjection, ["available", "version"])
      || activePropProjection.version !== 1
      || activePropProjection.available !== false
    ) {
      return false;
    }
  }
  return true;
}

function validIdentityRegistrationAndSealedMvpBundleGate3Report(report) {
  return validIdentityRegistrationAndIdleStorageBase(report)
    && validIdentityRegistrationAndSealedMvpBundle(
      report.assetAuthoring,
      report.publication,
    );
}

function validTimedReceipt(record, prefix) {
  return exactKeys(record, ["elapsedMs", "receipt"])
    && Number.isSafeInteger(record.elapsedMs)
    && record.elapsedMs >= 0
    && typeof record.receipt === "string"
    && (prefix === undefined || record.receipt.startsWith(prefix));
}

function publicationCatalog(publication) {
  const catalog = receiptFields(publication?.completed?.receipt).catalog;
  return typeof catalog === "string" && catalog.length > 0
    ? catalog
    : undefined;
}

function validRetentionRoundReceipt(record, publication, round, prefix, catalog) {
  if (!validTimedReceipt(record, prefix)) return false;
  const fields = receiptFields(record.receipt);
  const count = String(publication.objects.length);
  return exactKeys(fields, [
    "catalog",
    "native_available",
    "native_total",
    "published",
    "retention",
    "retention_round",
    "source",
    "state",
    "total",
    "verified",
  ])
    && fields.state === "verified"
    && fields.published === count
    && fields.verified === count
    && fields.total === count
    && fields.retention === "verified"
    && fields.retention_round === String(round)
    && fields.source === "cache"
    && fields.native_available === count
    && fields.native_total === count
    && fields.catalog === catalog;
}

function validRetainedCatalogObjects(retained, publication) {
  if (
    !Array.isArray(retained)
    || retained.length !== publication.objects.length
    || new Set(publication.objects.map((object) => object.objectId)).size
      !== publication.objects.length
  ) {
    return false;
  }
  const objectsById = new Map(
    publication.objects.map((object) => [object.objectId, object]),
  );
  const retainedIds = retained.map((object) => object?.objectId);
  if (new Set(retainedIds).size !== retainedIds.length) return false;
  return retained.every((record) => {
    const object = objectsById.get(record?.objectId);
    if (
      !object
      || !exactKeys(record, ["cid", "elapsedMs", "objectId", "receipt"])
      || record.cid !== object.cid
      || !Number.isSafeInteger(record.elapsedMs)
      || record.elapsedMs < 0
      || typeof record.receipt !== "string"
    ) {
      return false;
    }
    const fields = receiptFields(record.receipt);
    return exactKeys(fields, ["cid", "publication", "retention", "state"])
      && fields.state === "verified"
      && fields.publication === "published"
      && fields.retention === "verified"
      && fields.cid === object.cid;
  });
}

function validPostCreatorOfflineRetentionProofs(proofs, publication) {
  const proofDescription =
    "native exists(cid)=true for every CID, then local-only Storage V2 retrieval "
    + "with exact length, SHA-256, and bytes";
  const catalog = publicationCatalog(publication);
  return Array.isArray(proofs)
    && catalog !== undefined
    && proofs.length === 2
    && proofs.every((proof, index) => {
      const round = index + 1;
      return exactKeys(proof, [
        "completed",
        "dispatched",
        "proof",
        "retained",
        "round",
      ])
        && proof.round === round
        && proof.proof === proofDescription
        && validRetentionRoundReceipt(
          proof.dispatched,
          publication,
          round,
          "ok;",
          catalog,
        )
        && validRetentionRoundReceipt(
          proof.completed,
          publication,
          round,
          undefined,
          catalog,
        )
        && validRetainedCatalogObjects(proof.retained, publication);
    });
}

function validCatalogObjectReceiptList(
  records,
  publication,
  state,
  includesCid = state === "verified",
) {
  if (
    !Array.isArray(records)
    || records.length !== publication.objects.length
    || new Set(records.map((record) => record?.objectId)).size !== records.length
  ) {
    return false;
  }
  const objectsById = new Map(
    publication.objects.map((object) => [object.objectId, object]),
  );
  return records.every((record) => {
    const object = objectsById.get(record?.objectId);
    if (
      !object
      || !Number.isSafeInteger(record?.elapsedMs)
      || record.elapsedMs < 0
      || typeof record.receipt !== "string"
    ) {
      return false;
    }
    if (state === "missing") {
      return exactKeys(record, ["elapsedMs", "objectId", "receipt"])
        && record.receipt === "state=missing";
    }
    if (
      !exactKeys(record, [
        ...(includesCid ? ["cid"] : []),
        "elapsedMs",
        "objectId",
        "receipt",
      ])
      || (includesCid && record.cid !== object.cid)
    ) {
      return false;
    }
    const fields = receiptFields(record.receipt);
    return exactKeys(fields, ["cid", "publication", "retention", "state"])
      && fields.state === "verified"
      && fields.publication === "published"
      && fields.retention === "missing"
      && fields.cid === object.cid;
  });
}

function validBundleStatusReceipt(
  record,
  publication,
  state,
  prefix,
  catalog,
) {
  if (!validTimedReceipt(record, prefix)) return false;
  const fields = receiptFields(record.receipt);
  const count = String(publication.objects.length);
  return exactKeys(fields, [
    ...(catalog === undefined ? [] : ["catalog"]),
    "native_available",
    "native_total",
    "published",
    "retention",
    "retention_round",
    "source",
    "state",
    "total",
    "verified",
  ])
    && fields.state === state
    && fields.published === count
    && fields.total === count
    && fields.retention === "missing"
    && fields.retention_round === "0"
    && fields.source === "cache"
    && fields.native_available === count
    && fields.native_total === count
    && (catalog === undefined || fields.catalog === catalog)
    && (
      state === "fetching"
        ? fields.verified === "0"
        : fields.verified === count
    );
}

function validPostCreatorOfflineBundleFetch(fetch, publication, mode) {
  const catalog = publicationCatalog(publication);
  if (
    !exactKeys(fetch, [
      "before",
      "clock",
      "completed",
      "dispatched",
      "endBoundary",
      "endToEndMs",
      "mode",
      "startBoundary",
      "verified",
    ])
    || fetch.mode !== mode
    || fetch.clock !== "performance.now monotonic milliseconds"
    || fetch.startBoundary
      !== "immediately before first local object status read"
    || fetch.endBoundary
      !== "all exact catalog objects re-read as CID-verified after completion"
    || !Number.isSafeInteger(fetch.endToEndMs)
    || fetch.endToEndMs < 0
    || catalog === undefined
    || !validCatalogObjectReceiptList(
      fetch.before,
      publication,
      mode === "network" ? "missing" : "verified",
      false,
    )
    || !validCatalogObjectReceiptList(fetch.verified, publication, "verified")
  ) {
    return false;
  }
  if (mode === "network") {
    return validBundleStatusReceipt(
      fetch.dispatched,
      publication,
      "fetching",
      "ok;",
      undefined,
    )
      && validBundleStatusReceipt(
        fetch.completed,
        publication,
        "verified",
        undefined,
        catalog,
      );
  }
  const cacheDispatchFields = receiptFields(fetch.dispatched?.receipt);
  return exactKeys(fetch.dispatched, [
    "elapsedMs",
    "receipt",
    "reusedVerifiedCatalog",
  ])
    && fetch.dispatched.reusedVerifiedCatalog === true
    && fetch.dispatched.elapsedMs === 0
    && typeof fetch.dispatched.receipt === "string"
    && exactKeys(cacheDispatchFields, ["catalog", "state"])
    && cacheDispatchFields.state === "verified"
    && cacheDispatchFields.catalog === catalog
    && validBundleStatusReceipt(
      fetch.completed,
      publication,
      "verified",
      undefined,
      catalog,
    );
}

function validStoragePeerEndpoint(endpoint) {
  return exactKeys(endpoint, [
    "addrs",
    "announceAddresses",
    "peerId",
    "spr",
    "tablePeers",
  ])
    && typeof endpoint.peerId === "string"
    && endpoint.peerId.length > 0
    && typeof endpoint.spr === "string"
    && endpoint.spr.startsWith("spr:")
    && Array.isArray(endpoint.addrs)
    && endpoint.addrs.length > 0
    && endpoint.addrs.every((address) =>
      typeof address === "string" && address.length > 0,
    )
    && exactJson(endpoint.announceAddresses, endpoint.addrs)
    && Array.isArray(endpoint.tablePeers)
    && endpoint.tablePeers.length > 0
    && endpoint.tablePeers.every((peerId) =>
      typeof peerId === "string" && peerId.length > 0,
    )
    && endpoint.tablePeers.includes(endpoint.peerId);
}

function validStorageMeshDial(dial, endpoints) {
  if (
    !exactKeys(dial, ["addresses", "from", "peerId", "result", "to"])
    || !["a", "b"].includes(dial.from)
    || !["a", "b"].includes(dial.to)
    || dial.from === dial.to
  ) {
    return false;
  }
  const destination = endpoints[dial.to];
  const expectedAddresses = [
    ...destination.addrs,
    ...destination.addrs.map(
      (address) => `${address}/p2p/${destination.peerId}`,
    ),
  ];
  const fields = receiptFields(dial.result?.receipt);
  return dial.peerId === destination.peerId
    && exactJson(dial.addresses, expectedAddresses)
    && validTimedReceipt(dial.result, "ok;")
    && exactKeys(fields, ["connect", "peers"])
    && fields.connect === "sent"
    && fields.peers === "2";
}

function validStorageMesh(mesh, endpoints) {
  if (
    !exactKeys(mesh, ["dials", "labels", "settleMs"])
    || !exactJson(mesh.labels, ["a", "b"])
    || !Array.isArray(mesh.dials)
    || mesh.dials.length !== 2
    || !Number.isSafeInteger(mesh.settleMs)
    || mesh.settleMs < 0
    || !mesh.dials.every((dial) => validStorageMeshDial(dial, endpoints))
  ) {
    return false;
  }
  const routes = new Set(mesh.dials.map((dial) => `${dial.from}->${dial.to}`));
  return routes.size === 2 && routes.has("a->b") && routes.has("b->a");
}

function validStorageMeshVisibility(visibility, endpoints) {
  if (
    !exactKeys(visibility, ["endpoints", "ready", "waitedMs"])
    || visibility.ready !== true
    || !Number.isSafeInteger(visibility.waitedMs)
    || visibility.waitedMs < 0
    || !exactKeys(visibility.endpoints, ["a", "b"])
  ) {
    return false;
  }
  return ["a", "b"].every((label) => {
    const endpoint = visibility.endpoints[label];
    const expected = endpoints[label];
    const otherPeerId = endpoints[label === "a" ? "b" : "a"].peerId;
    if (
      !exactKeys(endpoint, [
        "addrs",
        "announceAddresses",
        "elapsedMs",
        "peerId",
        "receipt",
        "seenPeers",
        "spr",
        "tablePeers",
      ])
      || endpoint.peerId !== expected.peerId
      || endpoint.spr !== expected.spr
      || !exactJson(endpoint.addrs, expected.addrs)
      || !exactJson(endpoint.announceAddresses, expected.announceAddresses)
      || !Array.isArray(endpoint.tablePeers)
      || new Set(endpoint.tablePeers).size !== 2
      || !endpoint.tablePeers.includes(expected.peerId)
      || !endpoint.tablePeers.includes(otherPeerId)
      || !exactJson(endpoint.seenPeers, [otherPeerId])
      || !Number.isSafeInteger(endpoint.elapsedMs)
      || endpoint.elapsedMs < 0
      || typeof endpoint.receipt !== "string"
      || !endpoint.receipt.startsWith("ok;")
    ) {
      return false;
    }
    try {
      return exactJson(JSON.parse(endpoint.receipt.slice(3)), {
        addrs: endpoint.addrs,
        announceAddresses: endpoint.announceAddresses,
        peerId: endpoint.peerId,
        seenPeers: endpoint.seenPeers,
        spr: endpoint.spr,
        tablePeers: endpoint.tablePeers,
      });
    } catch {
      return false;
    }
  });
}

function validStorageBlockMaterialization(materialization) {
  return exactKeys(materialization, [
    "copied",
    "fromLabel",
    "fromRepo",
    "mode",
    "toLabel",
    "toRepo",
  ])
    && materialization.fromLabel === "a"
    && materialization.toLabel === "b"
    && materialization.mode === "co-located-block-copy"
    && typeof materialization.fromRepo === "string"
    && materialization.fromRepo.length > 0
    && typeof materialization.toRepo === "string"
    && materialization.toRepo.length > 0
    && materialization.fromRepo !== materialization.toRepo
    && exactJson(materialization.copied, [
      "blocks",
      "manifests",
      "dht/providers",
      "storage_publications",
      "verified_assets",
    ]);
}

function validPostCreatorOfflineStorageTopology(report) {
  const endpoints = report.storagePeerEndpoints;
  return exactKeys(endpoints, ["a", "b"])
    && validStoragePeerEndpoint(endpoints.a)
    && validStoragePeerEndpoint(endpoints.b)
    && endpoints.a.peerId !== endpoints.b.peerId
    && validStorageMesh(report.storageMesh, endpoints)
    && validStorageMesh(report.storageMeshPreFetch, endpoints)
    && exactJson(report.storageMeshPreFetch, report.storageMesh)
    && validStorageMeshVisibility(report.storageMeshVisibility, endpoints)
    && validStorageBlockMaterialization(report.storageBlockMaterialization);
}

function validPostCreatorOfflineFetchFailure(failure) {
  const match = /^worker b: gate3FetchPng receipt timeout: before="missing" after="" state="([^"]+)" sequence=([1-9][0-9]*)->([1-9][0-9]*)$/.exec(
    failure,
  );
  return match !== null
    && Number(match[3]) === Number(match[2]) + 1
    && validCurrentLezAuthorityFields(receiptFields(match[1]));
}

function validIdentityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline(
  report,
) {
  return validIdentityRegistrationAndSealedMvpBundleGate3Report(report)
    && exactKeys(report.creatorStopIntent, ["checkpointed", "pid"])
    && report.creatorStopIntent.checkpointed === true
    && Number.isSafeInteger(report.creatorStopIntent.pid)
    && report.creatorStopIntent.pid > 1
    && report.creatorStopIntent.pid === report.startup.a.basecampPid
    && report.creatorOffline === true
    && validPostCreatorOfflineRetentionProofs(
      report.providerBRetentionProofs,
      report.publication,
    )
    && validPostCreatorOfflineBundleFetch(
      report.providerBFetch,
      report.publication,
      "network",
    )
    && validPostCreatorOfflineBundleFetch(
      report.providerBCachedFetch,
      report.publication,
      "cache",
    )
    && validPostCreatorOfflineFetchFailure(report.failure);
}

function validatesAuditedPrePublicWriteGate3Report(
  report,
  predecessor,
  audit,
) {
  const identityRegistrationAndIdleStorage =
    audit.reportProfile === identityRegistrationAndIdleStorageProfile;
  const identityRegistrationAndApprovalGuardedAssets =
    audit.reportProfile
      === identityRegistrationAndApprovalGuardedAssetsProfile;
  const identityRegistrationAndPublishedAssets =
    audit.reportProfile === identityRegistrationAndPublishedAssetsProfile;
  const identityRegistrationAndSealedMvpBundle =
    audit.reportProfile === identityRegistrationAndSealedMvpBundleProfile;
  const identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline =
    audit.reportProfile
      === identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOfflineProfile;
  const hasSealedMvpBundle = identityRegistrationAndSealedMvpBundle
    || identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline;
  const hasAssetAuthoring = identityRegistrationAndIdleStorage
    || identityRegistrationAndApprovalGuardedAssets
    || identityRegistrationAndPublishedAssets
    || hasSealedMvpBundle;
  // Gate 3 may attach a path-free screenshot evidence object after authoring
  // completes and still fail later (e.g. MVP bundle publish timeout).
  const hasAssetAuthoringScreenshot = Object.hasOwn(
    report,
    "assetAuthoringScreenshot",
  );
  const hasPublication = Object.hasOwn(report, "publication");
  const hasCreatorStopIntent = Object.hasOwn(report, "creatorStopIntent");
  // Optional multi-node mesh evidence written after Storage start; present
  // when peer bootstrap/connect ran before a later pre-public-write failure.
  const hasStoragePeerEndpoints = Object.hasOwn(
    report,
    "storagePeerEndpoints",
  );
  const hasStorageMesh = Object.hasOwn(report, "storageMesh");
  const hasStorageMeshC = Object.hasOwn(report, "storageMeshC");
  const hasStorageMeshPreFetch = Object.hasOwn(
    report,
    "storageMeshPreFetch",
  );
  const hasStorageMeshVisibility = Object.hasOwn(
    report,
    "storageMeshVisibility",
  );
  const hasStorageBlockMaterialization = Object.hasOwn(
    report,
    "storageBlockMaterialization",
  );
  const hasStorageBlockMaterializationC = Object.hasOwn(
    report,
    "storageBlockMaterializationC",
  );
  // Peer fetch can succeed (providerBFetch/providerBCachedFetch) and still fail
  // later on retention (BOnEa1w7) before any LEZ public write.
  const hasProviderBFetch = Object.hasOwn(report, "providerBFetch");
  const hasProviderBCachedFetch = Object.hasOwn(
    report,
    "providerBCachedFetch",
  );
  if (
    !exactKeys(report, [
      ...(hasAssetAuthoring ? ["assetAuthoring"] : []),
      ...(hasAssetAuthoringScreenshot
        ? ["assetAuthoringScreenshot"]
        : []),
      "basecampBinarySha256",
      "basecampRevision",
      "blockers",
      "cleanup",
      "creatorOffline",
      ...(hasCreatorStopIntent ? ["creatorStopIntent"] : []),
      "failure",
      "fullGate3",
      "identities",
      "installedPackages",
      "packageHashes",
      "pngRecovery",
      "productSnapshot",
      "productSnapshotNarHash",
      "productSnapshotNarSize",
      "productionIdentityMode",
      ...(hasProviderBCachedFetch ? ["providerBCachedFetch"] : []),
      ...(hasProviderBFetch ? ["providerBFetch"] : []),
      "providerBRetentionProofs",
      ...(hasPublication ? ["publication"] : []),
      "releasePreflight",
      "runtimeOutputManifestSha256",
      "schema",
      "snapshotRunnerSha256",
      "sourceCommit",
      "startup",
      "status",
      "storageConfigs",
      ...(hasStorageMesh ? ["storageMesh"] : []),
      ...(hasStorageMeshC ? ["storageMeshC"] : []),
      ...(hasStorageMeshPreFetch ? ["storageMeshPreFetch"] : []),
      ...(hasStorageMeshVisibility ? ["storageMeshVisibility"] : []),
      ...(hasStorageBlockMaterialization ? ["storageBlockMaterialization"] : []),
      ...(hasStorageBlockMaterializationC ? ["storageBlockMaterializationC"] : []),
      ...(hasStoragePeerEndpoints ? ["storagePeerEndpoints"] : []),
      "storageStartup",
      "version",
    ])
    || report.schema !== "logos.palace.basecamp-gate3-report"
    || report.version !== 1
    || report.status !== "failed"
    || report.fullGate3 !== "failed"
    || report.productSnapshot !== predecessor.productSnapshot
    || report.sourceCommit !== predecessor.gitCommit
    || report.productSnapshotNarHash !== predecessor.snapshotNarHash
    || report.productSnapshotNarSize !== predecessor.snapshotNarSize
    || report.snapshotRunnerSha256 !== predecessor.snapshotRunnerSha256
    || report.runtimeOutputManifestSha256
      !== predecessor.runtimeManifestSha256
    || report.productionIdentityMode !== true
    || report.failure !== audit.gate3Failure
    || !exactKeys(report.cleanup, ["failures", "status"])
    || !(
      (
        report.cleanup.status === "passed"
        && exactJson(report.cleanup.failures, [])
      )
      // Sealed-catalog peer-fetch failures can race worker teardown so pidfd
      // cleanup sees ESRCH for already-reaped Basecamp processes. That is not
      // a LEZ/public-write side effect; allow only those cleanup failures.
      || (
        hasSealedMvpBundle
        && report.cleanup.status === "failed"
        && Array.isArray(report.cleanup.failures)
        && report.cleanup.failures.length > 0
        && report.cleanup.failures.every((failure) =>
          typeof failure === "string"
          && failure.includes("ESRCH")
          && failure.includes("cleanup rejected")
        )
      )
    )
    || !exactJson(report.blockers, [])
    || (
      hasAssetAuthoring
        ? !(
          identityRegistrationAndIdleStorage
            ? validIdentityRegistrationAndIdleStorageGate3Report(report)
            : identityRegistrationAndApprovalGuardedAssets
              ? validIdentityRegistrationAndApprovalGuardedAssetsGate3Report(
                report,
              )
              : hasSealedMvpBundle
                ? validIdentityRegistrationAndSealedMvpBundleGate3Report(
                  report,
                )
              : validIdentityRegistrationAndPublishedAssetsGate3Report(report)
        )
        : (
          !exactKeys(report.identities, [])
          || !exactKeys(report.storageStartup, [])
          || !exactKeys(report.startup, ["a"])
          || !exactKeys(report.startup.a, ["basecampPid", "startupMs"])
          || !Number.isSafeInteger(report.startup.a.basecampPid)
          || report.startup.a.basecampPid <= 1
          || !Number.isSafeInteger(report.startup.a.startupMs)
          || report.startup.a.startupMs < 0
        )
    )
    || (hasSealedMvpBundle !== hasPublication)
    || (
      identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline
      && (
        !hasCreatorStopIntent
        || !hasProviderBFetch
        || !hasProviderBCachedFetch
        || !hasStoragePeerEndpoints
        || !hasStorageMesh
        || !hasStorageMeshPreFetch
        || !hasStorageMeshVisibility
        || !hasStorageBlockMaterialization
        || hasStorageMeshC
        || hasStorageBlockMaterializationC
        || !validPostCreatorOfflineStorageTopology(report)
        || !validIdentityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline(
          report,
        )
      )
    )
    || (
      !identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline
      && hasCreatorStopIntent
    )
    || !Array.isArray(report.providerBRetentionProofs)
    || (
      identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline
        ? false
        : report.providerBRetentionProofs.length !== 0
    )
    || (
      identityRegistrationAndSealedMvpBundleRetainedAfterCreatorOffline
        ? report.creatorOffline !== true
        : report.creatorOffline !== false
    )
    || report.pngRecovery !== "failed"
    || report.releasePreflight?.status !== "passed"
    || report.releasePreflight?.rootAccountBeforeWrites?.status !== "passed"
    || report.releasePreflight.rootAccountBeforeWrites.state
      !== "uninitialized"
  ) {
    throw new Error("audited Gate 3 pre-public-write report is invalid");
  }
}

function prePublicWriteRetirementRecord({
  predecessor,
  compiledReportSha256,
  gate3ReportSha256,
  retirementStatus,
}) {
  return {
    schema: "logos.palace.basecamp-pre-public-write-retirement",
    version: 1,
    status: retirementStatus,
    predecessor: {
      gitCommit: predecessor.gitCommit,
      snapshotNarHash: predecessor.snapshotNarHash,
      snapshotNarSize: predecessor.snapshotNarSize,
      snapshotRunnerSha256: predecessor.snapshotRunnerSha256,
      runtimeManifestSha256: predecessor.runtimeManifestSha256,
      compiledReportSha256,
      gate3ReportSha256,
    },
  };
}

function validatePrePublicWriteRetirementRecord({
  record,
  predecessor,
  audit,
}) {
  if (
    !exactKeys(record, ["predecessor", "schema", "status", "version"])
    || record.schema !== "logos.palace.basecamp-pre-public-write-retirement"
    || record.version !== 1
    || record.status !== audit.retirementStatus
    || !exactKeys(record.predecessor, [
      "compiledReportSha256",
      "gate3ReportSha256",
      "gitCommit",
      "runtimeManifestSha256",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
    ])
    || record.predecessor.gitCommit !== predecessor.gitCommit
    || record.predecessor.snapshotNarHash !== predecessor.snapshotNarHash
    || record.predecessor.snapshotNarSize !== predecessor.snapshotNarSize
    || record.predecessor.snapshotRunnerSha256
      !== predecessor.snapshotRunnerSha256
    || record.predecessor.runtimeManifestSha256
      !== predecessor.runtimeManifestSha256
    || record.predecessor.compiledReportSha256
      !== audit.compiledReportSha256
    || record.predecessor.gate3ReportSha256 !== audit.gate3ReportSha256
  ) {
    throw new Error("pre-public-write retirement certificate is invalid");
  }
  return record;
}

async function validateAuditedPrePublicWriteGate3Artifacts({
  predecessor,
  uid,
  audits,
}) {
  const audit = audits.find((candidate) =>
    matchesAuditedPrePublicWriteFailure(predecessor, candidate));
  if (!audit) {
    throw new Error("Gate 3 predecessor is not the audited pre-write failure");
  }
  const run = predecessor.runDirectory;
  await canonicalOwnerDirectory(join(run, "gate3"), uid, 0o700);
  await absent(join(run, "gate4"), "pre-public-write Gate 4 evidence");
  await absent(
    join(run, "active-claim-completion.json"),
    "pre-public-write claim completion",
  );
  await absent(
    join(run, "public-evidence.json"),
    "pre-public-write public evidence",
  );
  await canonicalOwnerDirectory(join(run, "shared-state"), uid, 0o700);

  const [compiled, gate3] = await Promise.all([
    parseSecureJson(
      join(run, "compiled-mvp-report.json"),
      uid,
      4 * 1024 * 1024,
      "audited Gate 3 compiled report",
    ),
    parseSecureJson(
      join(run, "gate3", "gate3-report.json"),
      uid,
      4 * 1024 * 1024,
      "audited Gate 3 report",
    ),
  ]);
  if (
    compiled.sha256 !== audit.compiledReportSha256
    || gate3.sha256 !== audit.gate3ReportSha256
  ) {
    throw new Error("audited Gate 3 report digest differs");
  }
  validateAuditedGate3FailedReport(compiled.value, predecessor);
  validatesAuditedPrePublicWriteGate3Report(
    gate3.value,
    predecessor,
    audit,
  );
  return {
    compiledReportSha256: compiled.sha256,
    gate3ReportSha256: gate3.sha256,
    audit,
  };
}

function rollForwardRecord({
  predecessor,
  predecessorClaimSha256,
  predecessorCompiledReportSha256,
  retiredClaimArchiveSha256,
  common,
  failurePhase,
}) {
  return {
    schema: rollForwardSchema,
    version: 1,
    status: "retired-before-gate3",
    predecessor: {
      claimVersion: predecessor.version,
      claimSha256: predecessorClaimSha256,
      compiledReportSha256: predecessorCompiledReportSha256,
      failurePhase,
      gitCommit: predecessor.gitCommit,
      snapshotNarHash: predecessor.snapshotNarHash,
      snapshotNarSize: predecessor.snapshotNarSize,
      snapshotRunnerSha256: predecessor.snapshotRunnerSha256,
      runtimeManifestSha256: predecessor.runtimeManifestSha256,
    },
    successor: {
      gitCommit: common.gitCommit,
      snapshotNarHash: common.snapshotNarHash,
      snapshotNarSize: common.snapshotNarSize,
      snapshotRunnerSha256: common.snapshotRunnerSha256,
      runtimeManifestSha256: common.runtimeManifestSha256,
      processScopeSlice: common.processScopeSlice,
      processScopePrefix: common.processScopePrefix,
    },
    proof: {
      releaseLock: "held-exclusive",
      claimBoundProcessCount: 0,
      compiledFailure: "verified",
      gate3ClaimState: "never-entered",
      gate3Artifacts: "absent",
      gate4Artifacts: "absent",
      sharedState: "empty",
      completion: "absent",
      publicEvidence: "absent",
      retiredClaimArchiveSha256,
    },
  };
}

function prePublicWriteRollForwardRecord({
  predecessor,
  predecessorClaimSha256,
  retiredClaimArchiveSha256,
  common,
  compiledReportSha256,
  gate3ReportSha256,
  retirementCertificateSha256,
  retirementStatus,
}) {
  return {
    schema: rollForwardSchema,
    version: 2,
    status: "retired-pre-public-write",
    predecessor: {
      claimVersion: predecessor.version,
      claimSha256: predecessorClaimSha256,
      compiledReportSha256,
      gate3ReportSha256,
      gitCommit: predecessor.gitCommit,
      snapshotNarHash: predecessor.snapshotNarHash,
      snapshotNarSize: predecessor.snapshotNarSize,
      snapshotRunnerSha256: predecessor.snapshotRunnerSha256,
      runtimeManifestSha256: predecessor.runtimeManifestSha256,
    },
    successor: {
      gitCommit: common.gitCommit,
      snapshotNarHash: common.snapshotNarHash,
      snapshotNarSize: common.snapshotNarSize,
      snapshotRunnerSha256: common.snapshotRunnerSha256,
      runtimeManifestSha256: common.runtimeManifestSha256,
      processScopeSlice: common.processScopeSlice,
      processScopePrefix: common.processScopePrefix,
    },
    proof: {
      releaseLock: "held-exclusive",
      claimBoundProcessCount: 0,
      compiledFailure: "audited-pre-public-write",
      gate3ClaimState: "entered",
      gate3Artifacts: retirementStatus,
      gate4Artifacts: "absent",
      sharedState: "local-only-retained",
      completion: "absent",
      publicEvidence: "absent",
      retirementCertificate: "pre-public-write-gate3-retirement.json",
      retirementCertificateSha256,
      retiredClaimArchiveSha256,
    },
  };
}

function validatePreGate3RollForwardRecord({
  record,
  claim,
  archivedClaim,
  archivedClaimSha256,
}) {
  if (
    !exactKeys(record, [
      "schema",
      "version",
      "status",
      "predecessor",
      "successor",
      "proof",
    ])
    || record.schema !== rollForwardSchema
    || record.version !== 1
    || record.status !== "retired-before-gate3"
    || !exactKeys(record.predecessor, [
      "claimVersion",
      "claimSha256",
      "compiledReportSha256",
      "failurePhase",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
    ])
    || record.predecessor.claimVersion !== archivedClaim.version
    || record.predecessor.claimSha256 !== archivedClaimSha256
    || record.predecessor.compiledReportSha256
      !== claim.rollForward.predecessorCompiledReportSha256
    || (
      archivedClaim.version === 1
        ? record.predecessor.failurePhase !== "gate1"
        : !preGate3Phases.has(record.predecessor.failurePhase)
    )
    || record.predecessor.gitCommit !== archivedClaim.gitCommit
    || record.predecessor.snapshotNarHash !== archivedClaim.snapshotNarHash
    || record.predecessor.snapshotNarSize !== archivedClaim.snapshotNarSize
    || record.predecessor.snapshotRunnerSha256
      !== archivedClaim.snapshotRunnerSha256
    || record.predecessor.runtimeManifestSha256
      !== archivedClaim.runtimeManifestSha256
    || !exactKeys(record.successor, [
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ])
    || record.successor.gitCommit !== claim.gitCommit
    || record.successor.snapshotNarHash !== claim.snapshotNarHash
    || record.successor.snapshotNarSize !== claim.snapshotNarSize
    || record.successor.snapshotRunnerSha256 !== claim.snapshotRunnerSha256
    || record.successor.runtimeManifestSha256
      !== claim.runtimeManifestSha256
    || record.successor.processScopeSlice !== claim.processScopeSlice
    || record.successor.processScopePrefix !== claim.processScopePrefix
    || !exactKeys(record.proof, [
      "releaseLock",
      "claimBoundProcessCount",
      "compiledFailure",
      "gate3ClaimState",
      "gate3Artifacts",
      "gate4Artifacts",
      "sharedState",
      "completion",
      "publicEvidence",
      "retiredClaimArchiveSha256",
    ])
    || record.proof.releaseLock !== "held-exclusive"
    || record.proof.claimBoundProcessCount !== 0
    || record.proof.compiledFailure !== "verified"
    || record.proof.gate3ClaimState !== "never-entered"
    || record.proof.gate3Artifacts !== "absent"
    || record.proof.gate4Artifacts !== "absent"
    || record.proof.sharedState !== "empty"
    || record.proof.completion !== "absent"
    || record.proof.publicEvidence !== "absent"
    || record.proof.retiredClaimArchiveSha256 !== archivedClaimSha256
  ) {
    throw new Error("active-run roll-forward evidence is invalid");
  }
  return record;
}

async function validatePrePublicWriteRollForwardRecord({
  record,
  claim,
  archivedClaim,
  archivedClaimSha256,
  uid,
  audits,
}) {
  const audit = audits.find((candidate) =>
    matchesAuditedPrePublicWriteFailure(archivedClaim, candidate));
  const retirementCertificate = record?.proof?.retirementCertificate;
  if (
    !exactKeys(record, [
      "schema",
      "version",
      "status",
      "predecessor",
      "successor",
      "proof",
    ])
    || record.schema !== rollForwardSchema
    || record.version !== 2
    || record.status !== "retired-pre-public-write"
    || archivedClaim.version !== 2
    || archivedClaim.status !== "gate3-entered"
    || !audit
    || !matchesAuditedPrePublicWriteFailure(archivedClaim, audit)
    || !exactKeys(record.predecessor, [
      "claimVersion",
      "claimSha256",
      "compiledReportSha256",
      "gate3ReportSha256",
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
    ])
    || record.predecessor.claimVersion !== archivedClaim.version
    || record.predecessor.claimSha256 !== archivedClaimSha256
    || record.predecessor.compiledReportSha256
      !== claim.rollForward.predecessorCompiledReportSha256
    || record.predecessor.compiledReportSha256
      !== audit.compiledReportSha256
    || record.predecessor.gate3ReportSha256 !== audit.gate3ReportSha256
    || record.predecessor.gitCommit !== archivedClaim.gitCommit
    || record.predecessor.snapshotNarHash !== archivedClaim.snapshotNarHash
    || record.predecessor.snapshotNarSize !== archivedClaim.snapshotNarSize
    || record.predecessor.snapshotRunnerSha256
      !== archivedClaim.snapshotRunnerSha256
    || record.predecessor.runtimeManifestSha256
      !== archivedClaim.runtimeManifestSha256
    || !exactKeys(record.successor, [
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
      "processScopeSlice",
      "processScopePrefix",
    ])
    || record.successor.gitCommit !== claim.gitCommit
    || record.successor.snapshotNarHash !== claim.snapshotNarHash
    || record.successor.snapshotNarSize !== claim.snapshotNarSize
    || record.successor.snapshotRunnerSha256 !== claim.snapshotRunnerSha256
    || record.successor.runtimeManifestSha256
      !== claim.runtimeManifestSha256
    || record.successor.processScopeSlice !== claim.processScopeSlice
    || record.successor.processScopePrefix !== claim.processScopePrefix
    || !exactKeys(record.proof, [
      "releaseLock",
      "claimBoundProcessCount",
      "compiledFailure",
      "gate3ClaimState",
      "gate3Artifacts",
      "gate4Artifacts",
      "sharedState",
      "completion",
      "publicEvidence",
      "retirementCertificate",
      "retirementCertificateSha256",
      "retiredClaimArchiveSha256",
    ])
    || record.proof.releaseLock !== "held-exclusive"
    || record.proof.claimBoundProcessCount !== 0
    || record.proof.compiledFailure !== "audited-pre-public-write"
    || record.proof.gate3ClaimState !== "entered"
    || record.proof.gate3Artifacts !== audit.retirementStatus
    || record.proof.gate4Artifacts !== "absent"
    || record.proof.sharedState !== "local-only-retained"
    || record.proof.completion !== "absent"
    || record.proof.publicEvidence !== "absent"
    || retirementCertificate !== "pre-public-write-gate3-retirement.json"
    || !sha256Pattern.test(record.proof.retirementCertificateSha256)
    || record.proof.retiredClaimArchiveSha256 !== archivedClaimSha256
  ) {
    throw new Error("pre-public-write roll-forward evidence is invalid");
  }

  const certificatePath = join(
    archivedClaim.runDirectory,
    retirementCertificate,
  );
  if (dirname(certificatePath) !== archivedClaim.runDirectory) {
    throw new Error("pre-public-write retirement certificate path is invalid");
  }
  const certificate = await parseSecureJson(
    certificatePath,
    uid,
    64 * 1024,
    "pre-public-write retirement certificate",
  );
  if (certificate.sha256 !== record.proof.retirementCertificateSha256) {
    throw new Error("pre-public-write retirement certificate digest differs");
  }
  validatePrePublicWriteRetirementRecord({
    record: certificate.value,
    predecessor: archivedClaim,
    audit,
  });
  return record;
}

async function validateRollForwardRecord({
  record,
  claim,
  archivedClaim,
  archivedClaimSha256,
  uid,
  prePublicWriteAudits,
}) {
  if (record?.version === 1) {
    if (
      archivedClaim.version !== 1
      && archivedClaim.status !== "active-pre-gate3"
    ) {
      throw new Error("archived predecessor claim state is invalid");
    }
    return validatePreGate3RollForwardRecord({
      record,
      claim,
      archivedClaim,
      archivedClaimSha256,
    });
  }
  return validatePrePublicWriteRollForwardRecord({
    record,
    claim,
    archivedClaim,
    archivedClaimSha256,
    uid,
    audits: prePublicWriteAudits,
  });
}

async function assertNoClaimProcesses(scanClaimBoundProcesses, input) {
  const processes = await scanClaimBoundProcesses(input);
  if (!Array.isArray(processes) || processes.length !== 0) {
    throw new Error("predecessor retains claim-bound processes");
  }
}

function newClaim(common, now, rollForward) {
  return validateV2Claim({
    ...common,
    status: "active-pre-gate3",
    createdAtUnixMs: now,
    ...(rollForward ? { rollForward } : {}),
  }, common);
}

export function createClaimLifecycle({
  uid,
  claimDirectory,
  claimPath,
  common,
  now = () => Date.now(),
  assertReleaseLockHeld,
  scanClaimBoundProcesses,
  retirePredecessorScope,
  validateImmutableSnapshot,
  completedReportSha256,
  writeCompletionRecord,
  legacyAudit = auditedLegacyPreGate3,
  prePublicWriteAudits = auditedPrePublicWriteGate3Failures,
}) {
  validateCommonIdentity(common);
  if (
    uid !== common.uid
    || resolve(claimDirectory) !== claimDirectory
    || dirname(claimPath) !== claimDirectory
    || typeof assertReleaseLockHeld !== "function"
    || typeof scanClaimBoundProcesses !== "function"
    || typeof retirePredecessorScope !== "function"
    || typeof validateImmutableSnapshot !== "function"
    || typeof completedReportSha256 !== "function"
    || typeof writeCompletionRecord !== "function"
    || !exactKeys(legacyAudit, [
      "gitCommit",
      "snapshotNarHash",
      "snapshotNarSize",
      "snapshotRunnerSha256",
      "runtimeManifestSha256",
      "compiledReportSha256",
      "gate0ReportSha256",
      "gate1ReportSha256",
    ])
    || !sourceCommitPattern.test(legacyAudit.gitCommit)
    || !narHashPattern.test(legacyAudit.snapshotNarHash)
    || !Number.isSafeInteger(legacyAudit.snapshotNarSize)
    || legacyAudit.snapshotNarSize <= 0
    || !sha256Pattern.test(legacyAudit.snapshotRunnerSha256)
    || !sha256Pattern.test(legacyAudit.runtimeManifestSha256)
    || !sha256Pattern.test(legacyAudit.compiledReportSha256)
    || !sha256Pattern.test(legacyAudit.gate0ReportSha256)
    || !sha256Pattern.test(legacyAudit.gate1ReportSha256)
    || !validPrePublicWriteAudits(prePublicWriteAudits)
  ) {
    throw new Error("claim lifecycle dependencies are invalid");
  }

  async function validateRollForwardChain(claim, depth = 0, seen = new Set()) {
    if (!Object.hasOwn(claim, "rollForward")) return claim;
    // Bound must cover long audited recovery histories (Gate 3 iteration
    // chains can exceed dozens of nested retirements on one release claim).
    // Raised 32→64 after enmbmS9V hit the bound following multi-week peer-fetch
    // recovery (BOnEa1w7 et al.).
    if (depth >= 64) {
      throw new Error("active-run roll-forward chain exceeds depth bound");
    }
    const rollForward = claim.rollForward;
    const evidencePath = join(claim.runDirectory, rollForward.evidence);
    const archivePath = join(
      claim.runDirectory,
      rollForward.retiredClaimArchive,
    );
    if (
      dirname(evidencePath) !== claim.runDirectory
      || basename(evidencePath) !== "claim-roll-forward.json"
      || dirname(archivePath) !== claim.runDirectory
      || basename(archivePath) !== "retired-active-claim.json"
      || rollForward.retiredClaimArchiveSha256
        !== rollForward.predecessorClaimSha256
      || seen.has(rollForward.predecessorClaimSha256)
    ) {
      throw new Error("active-run roll-forward chain identity is invalid");
    }

    const [evidence, archive] = await Promise.all([
      parseSecureJson(
        evidencePath,
        uid,
        128 * 1024,
        "active-run roll-forward evidence",
      ),
      parseSecureJson(
        archivePath,
        uid,
        64 * 1024,
        "retired active-run claim archive",
      ),
    ]);
    if (
      evidence.sha256 !== rollForward.evidenceSha256
      || archive.sha256 !== rollForward.retiredClaimArchiveSha256
      || archive.sha256 !== rollForward.predecessorClaimSha256
    ) {
      throw new Error("active-run roll-forward evidence digest differs");
    }

    let archivedClaim;
    if (archive.value?.version === 1) {
      archivedClaim = validateLegacyClaim(archive.value, legacyAudit);
    } else {
      const archivedCommon = validateCommonIdentity(
        commonFromClaim(archive.value),
      );
      archivedClaim = validateV2Claim(archive.value, archivedCommon);
    }
    if (archivedClaim.uid !== uid) {
      throw new Error("archived predecessor claim state is invalid");
    }
    await validateRollForwardRecord({
      record: evidence.value,
      claim,
      archivedClaim,
      archivedClaimSha256: archive.sha256,
      uid,
      prePublicWriteAudits,
    });

    const nextSeen = new Set(seen);
    nextSeen.add(archive.sha256);
    if (archivedClaim.version === 2) {
      await validateRollForwardChain(archivedClaim, depth + 1, nextSeen);
    }
    return claim;
  }

  async function acquireOrRollForward() {
    await canonicalOwnerDirectory(claimDirectory, uid, 0o700);
    await assertReleaseLockHeld();
    const existing = await readClaim(claimPath, uid);
    if (!existing) {
      const claim = validateV2Claim(
        newClaim(common, transitionTimestamp(now, 1)),
        common,
      );
      try {
        await writeNewClaim(claimDirectory, claimPath, claim);
        return claim;
      } catch (error) {
        if (error?.code !== "EEXIST") throw error;
        const raced = await readClaim(claimPath, uid);
        const racedClaim = validateV2Claim(raced.value, common);
        return validateRollForwardChain(racedClaim);
      }
    }

    if (
      existing.value?.version === 2
      && matchesCommon(existing.value, common)
    ) {
      const current = validateV2Claim(existing.value, common);
      return validateRollForwardChain(current);
    }

    let predecessor;
    let auditedPrePublicWriteRecovery = false;
    let processScopeSlice;
    let processScopePrefix;
    if (existing.value?.version === 1) {
      predecessor = validateLegacyClaim(existing.value, legacyAudit);
      processScopeSlice = common.processScopeSlice;
      processScopePrefix = common.processScopePrefix;
    } else {
      const predecessorCommon = validateCommonIdentity(
        commonFromClaim(existing.value),
      );
      predecessor = validateV2Claim(existing.value, predecessorCommon);
      await validateRollForwardChain(predecessor);
      if (predecessor.status === "gate3-entered") {
        auditedPrePublicWriteRecovery = true;
      } else if (predecessor.status !== "active-pre-gate3") {
        throw new Error("predecessor claim already crossed Gate 3");
      }
      processScopeSlice = predecessor.processScopeSlice;
      processScopePrefix = predecessor.processScopePrefix;
    }

    await validatePredecessorIdentity({
      predecessor,
      common,
      uid,
      validateImmutableSnapshot,
    });
    const proof = auditedPrePublicWriteRecovery
      ? await validateAuditedPrePublicWriteGate3Artifacts({
          predecessor,
          uid,
          audits: prePublicWriteAudits,
        })
      : await validatePreGate3Artifacts({
          predecessor,
          claimVersion: predecessor.version,
          uid,
          legacyAudit,
        });
    const scanInput = {
      claimPath,
      processScopeSlice,
      processScopePrefix,
      legacy: predecessor.version === 1,
      createdAtUnixMs: predecessor.createdAtUnixMs,
    };
    if (predecessor.version === 2) {
      await retirePredecessorScope(scanInput);
    }
    await assertNoClaimProcesses(scanClaimBoundProcesses, scanInput);

    let retirementCertificateSha256;
    if (auditedPrePublicWriteRecovery) {
      const retirementCertificate = prePublicWriteRetirementRecord({
        predecessor,
        compiledReportSha256: proof.compiledReportSha256,
        gate3ReportSha256: proof.gate3ReportSha256,
        retirementStatus: proof.audit.retirementStatus,
      });
      const retirementCertificateBytes = Buffer.from(
        `${JSON.stringify(retirementCertificate, null, 2)}\n`,
        "utf8",
      );
      const retirementCertificatePath = join(
        predecessor.runDirectory,
        "pre-public-write-gate3-retirement.json",
      );
      await writeExclusiveDurable(
        retirementCertificatePath,
        retirementCertificateBytes,
        uid,
      );
      retirementCertificateSha256 = sha256(retirementCertificateBytes);
    }

    const archivePath = join(
      common.runDirectory,
      "retired-active-claim.json",
    );
    await writeExclusiveDurable(archivePath, existing.bytes, uid);
    const archiveSha256 = sha256(existing.bytes);
    const record = auditedPrePublicWriteRecovery
      ? prePublicWriteRollForwardRecord({
          predecessor,
          predecessorClaimSha256: existing.sha256,
          retiredClaimArchiveSha256: archiveSha256,
          common,
          compiledReportSha256: proof.compiledReportSha256,
          gate3ReportSha256: proof.gate3ReportSha256,
          retirementCertificateSha256,
          retirementStatus: proof.audit.retirementStatus,
        })
      : rollForwardRecord({
          predecessor,
          predecessorClaimSha256: existing.sha256,
          predecessorCompiledReportSha256: proof.compiledReportSha256,
          retiredClaimArchiveSha256: archiveSha256,
          common,
          failurePhase: proof.failurePhase,
        });
    const recordBytes = Buffer.from(
      `${JSON.stringify(record, null, 2)}\n`,
      "utf8",
    );
    const evidencePath = join(common.runDirectory, "claim-roll-forward.json");
    await writeExclusiveDurable(evidencePath, recordBytes, uid);

    const successor = validateV2Claim(
      newClaim(common, transitionTimestamp(now, 1), {
        evidence: basename(evidencePath),
        evidenceSha256: sha256(recordBytes),
        retiredClaimArchive: basename(archivePath),
        retiredClaimArchiveSha256: archiveSha256,
        predecessorClaimSha256: existing.sha256,
        predecessorCompiledReportSha256: proof.compiledReportSha256,
      }),
      common,
    );
    await validateRollForwardChain(successor);
    if (predecessor.version === 2) {
      await retirePredecessorScope(scanInput);
    }
    await assertNoClaimProcesses(scanClaimBoundProcesses, scanInput);
    await assertReleaseLockHeld();

    const unchanged = await secureFile(
      claimPath,
      uid,
      64 * 1024,
      "predecessor active-run claim",
    );
    if (sha256(unchanged) !== existing.sha256) {
      throw new Error("predecessor claim changed before replacement");
    }
    await durableReplaceClaim(claimDirectory, claimPath, successor);
    return successor;
  }

  async function exactCurrentClaim() {
    const existing = await readClaim(claimPath, uid);
    if (!existing) throw new Error("exact active-run claim does not exist");
    const claim = validateV2Claim(existing.value, common);
    return validateRollForwardChain(claim);
  }

  return {
    async execute(command) {
      if (command === "acquire-or-roll-forward") {
        const claim = await acquireOrRollForward();
        return { claim, output: claimPath };
      }

      let claim = await exactCurrentClaim();
      if (command === "state") {
        return { claim, output: claim.status };
      }
      if (command === "enter-gate3") {
        await assertReleaseLockHeld();
        if (claim.status === "active-pre-gate3") {
          claim = {
            ...claim,
            status: "gate3-entered",
            gate3EnteredAtUnixMs: transitionTimestamp(
              now,
              claim.createdAtUnixMs,
            ),
          };
          validateV2Claim(claim, common);
          await durableReplaceClaim(claimDirectory, claimPath, claim);
        } else if (claim.status !== "gate3-entered") {
          throw new Error("exact active-run claim cannot enter Gate 3");
        }
        return { claim, output: claimPath };
      }
      if (command === "verify") {
        await assertReleaseLockHeld();
        if (claim.status !== "gate3-entered") {
          throw new Error("exact active-run claim has not entered Gate 3");
        }
        return { claim, output: claimPath };
      }
      if (command === "complete") {
        await assertReleaseLockHeld();
        if (claim.status !== "gate3-entered") {
          throw new Error("exact active-run claim cannot complete");
        }
        const reportSha256 = await completedReportSha256();
        if (!sha256Pattern.test(reportSha256)) {
          throw new Error("completed claim report digest is invalid");
        }
        claim = {
          ...claim,
          status: "completed",
          completedAtUnixMs: transitionTimestamp(
            now,
            claim.gate3EnteredAtUnixMs,
          ),
          compiledReportSha256: reportSha256,
        };
        validateV2Claim(claim, common);
        await durableReplaceClaim(claimDirectory, claimPath, claim);
        return { claim, output: claimPath };
      }
      if (command === "completion") {
        await assertReleaseLockHeld();
        if (claim.status !== "completed") {
          throw new Error("exact active-run claim is not completed");
        }
        return {
          claim,
          output: await writeCompletionRecord(claim),
        };
      }
      throw new Error("active-run claim command is invalid");
    },
  };
}

export function completionRecord({
  claim,
  claimBytes,
  compiledReportSha256,
  common,
  completedAtUnixMs,
}) {
  return {
    schema: completionSchema,
    version: 1,
    status: "completed",
    completedAtUnixMs,
    activeClaimSha256: sha256(claimBytes),
    compiledReportSha256,
    productSnapshot: common.productSnapshot,
    sourceCommit: common.gitCommit,
    productSnapshotNarHash: common.snapshotNarHash,
    productSnapshotNarSize: common.snapshotNarSize,
    snapshotRunnerSha256: common.snapshotRunnerSha256,
    runtimeOutputManifestSha256: common.runtimeManifestSha256,
  };
}
