//! Versioned, bounded Palace records and deterministic transition rules.
//!
//! LEZ account authentication and PDA checks live in the guest adapter. This
//! crate owns the durable Palace schema, record-link checks, authority rules,
//! and the single contiguous ordered-action sequence carried by `PalaceRoot`.

use borsh::{BorshDeserialize, BorshSerialize};
use serde::{Deserialize, Serialize};

pub const SCHEMA_VERSION: u16 = 3;
pub const ROOM_COUNT: usize = 2;
pub const MAX_TITLE_BYTES: usize = 64;
pub const MAX_DISPLAY_NAME_BYTES: usize = 48;
pub const MAX_CID_BYTES: usize = 128;
pub const MAX_SHARED_KEY_BYTES: usize = 32;
pub const MAX_SHARED_VALUE_BYTES: usize = 512;
pub const MAX_USERS: u32 = 64;
pub const MAX_GRANTS: u32 = 64;
pub const MAX_BANS: u32 = 128;
pub const MAX_SHARED_STATES: u32 = 128;

pub const CAP_MODERATE_USER: u32 = 1 << 0;
pub const CAP_MODERATE_ASSET: u32 = 1 << 1;
pub const CAP_SET_ROOM_LOCK: u32 = 1 << 2;
pub const CAP_WRITE_SHARED_STATE: u32 = 1 << 3;
pub const CAP_ROOM_EDIT: u32 = 1 << 4;
pub const ALL_CAPABILITIES: u32 = CAP_MODERATE_USER
    | CAP_MODERATE_ASSET
    | CAP_SET_ROOM_LOCK
    | CAP_WRITE_SHARED_STATE
    | CAP_ROOM_EDIT;

pub type AccountId = [u8; 32];
pub type StableId = [u8; 32];
pub type RoomId = StableId;
pub type GrantId = StableId;
pub type BanId = StableId;
pub type SharedStateId = StableId;
pub type DeliveryKey = [u8; 32];
pub type StateRoot = [u8; 32];

#[derive(
    Clone, Copy, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize,
)]
pub enum RecordType {
    PalaceRoot,
    UserProfile,
    Room,
    CapabilityGrant,
    Ban,
    RoomSharedState,
}

#[derive(
    Clone, Copy, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize,
)]
pub enum VmProfile {
    NoScript,
    IptScraeMvpV1,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub enum PalaceScope {
    Palace,
    Room(RoomId),
}

impl PalaceScope {
    fn covers(&self, requested: &Self) -> bool {
        matches!(self, Self::Palace) || self == requested
    }

    fn as_room(&self) -> Option<&RoomId> {
        match self {
            Self::Palace => None,
            Self::Room(room_id) => Some(room_id),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub enum BanTarget {
    User(AccountId),
    AssetCid(String),
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct UserProfileInput {
    pub display_name: String,
    pub delivery_key: DeliveryKey,
    pub key_epoch: u64,
    pub avatar_manifest_cid: Option<String>,
}

impl UserProfileInput {
    pub fn validate(&self) -> Result<(), PalaceError> {
        if !valid_text(&self.display_name, MAX_DISPLAY_NAME_BYTES) {
            return Err(PalaceError::InvalidText);
        }
        if !valid_nonzero_id(&self.delivery_key) {
            return Err(PalaceError::InvalidDeliveryKey);
        }
        if self.key_epoch == 0 {
            return Err(PalaceError::KeyEpochNotAdvanced);
        }
        valid_optional_cid(&self.avatar_manifest_cid)
    }
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct RoomConfigInput {
    pub title: String,
    pub manifest_cid: String,
    pub script_bundle_cid: String,
    pub vm_profile: VmProfile,
}

impl RoomConfigInput {
    pub fn validate(&self) -> Result<(), PalaceError> {
        if !valid_text(&self.title, MAX_TITLE_BYTES) {
            return Err(PalaceError::InvalidText);
        }
        if !valid_cid(&self.manifest_cid) || !valid_cid(&self.script_bundle_cid) {
            return Err(PalaceError::InvalidCid);
        }
        if self.vm_profile == VmProfile::NoScript {
            return Err(PalaceError::InvalidVmProfile);
        }
        Ok(())
    }
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct PalaceRoot {
    pub record_type: RecordType,
    pub schema_version: u16,
    pub palace_id: StableId,
    pub title: String,
    pub owner: AccountId,
    pub entry_room_id: RoomId,
    pub room_ids: [RoomId; ROOM_COUNT],
    pub active_manifest_cid: String,
    pub user_count: u32,
    pub grant_count: u32,
    pub ban_count: u32,
    pub shared_state_count: u32,
    pub revision: u64,
    pub last_ordered_action_id: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct UserProfile {
    pub record_type: RecordType,
    pub schema_version: u16,
    pub palace_id: StableId,
    pub user_id: AccountId,
    pub display_name: String,
    pub delivery_key: DeliveryKey,
    pub key_epoch: u64,
    pub avatar_manifest_cid: Option<String>,
    pub profile_revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct RoomRecord {
    pub record_type: RecordType,
    pub schema_version: u16,
    pub palace_id: StableId,
    pub room_id: RoomId,
    pub title: String,
    pub manifest_cid: String,
    pub script_bundle_cid: String,
    pub vm_profile: VmProfile,
    pub locked: bool,
    pub revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct CapabilityGrant {
    pub record_type: RecordType,
    pub schema_version: u16,
    pub palace_id: StableId,
    pub grant_id: GrantId,
    pub subject_user_id: AccountId,
    pub issued_by: AccountId,
    pub scope: PalaceScope,
    pub capabilities: u32,
    pub delegable: bool,
    pub valid_through_action_id: u64,
    pub revoked: bool,
    pub revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct Ban {
    pub record_type: RecordType,
    pub schema_version: u16,
    pub palace_id: StableId,
    pub ban_id: BanId,
    pub target: BanTarget,
    pub issuer: AccountId,
    pub scope: PalaceScope,
    pub active: bool,
    pub revision: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct RoomSharedState {
    pub record_type: RecordType,
    pub schema_version: u16,
    pub palace_id: StableId,
    pub shared_state_id: SharedStateId,
    pub room_id: RoomId,
    pub key: String,
    pub value: Vec<u8>,
    pub state_root: StateRoot,
    pub revision: u64,
    pub last_ordered_action_id: u64,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct PalaceGenesis {
    pub root: PalaceRoot,
    pub owner_profile: UserProfile,
    pub rooms: [RoomRecord; ROOM_COUNT],
    pub owner_grant: CapabilityGrant,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum PalaceError {
    UnsupportedSchema,
    InvalidRooms,
    InvalidIdentifier,
    InvalidCid,
    InvalidDeliveryKey,
    Unauthorized,
    UnknownUser,
    UnknownRoom,
    KeyEpochNotAdvanced,
    OwnerCannotBeBanned,
    RecordRevisionOutOfSequence,
    RevisionExhausted,
    OrderedActionIdOutOfSequence,
    InvalidText,
    InvalidCapability,
    GrantExpired,
    GrantRevoked,
    RecordMismatch,
    LimitExceeded,
    InvalidValue,
    InvalidVmProfile,
    NoStateChange,
}

impl PalaceError {
    /// Stable Palace-specific rejection code used by guest and client handling.
    #[must_use]
    pub const fn code(&self) -> u32 {
        match self {
            Self::UnsupportedSchema => 1,
            Self::InvalidRooms => 2,
            Self::InvalidIdentifier => 3,
            Self::InvalidCid => 4,
            Self::InvalidDeliveryKey => 5,
            Self::Unauthorized => 6,
            Self::UnknownUser => 7,
            Self::UnknownRoom => 8,
            Self::KeyEpochNotAdvanced => 9,
            Self::OwnerCannotBeBanned => 10,
            Self::RecordRevisionOutOfSequence => 11,
            Self::RevisionExhausted => 12,
            Self::OrderedActionIdOutOfSequence => 13,
            Self::InvalidText => 14,
            Self::InvalidCapability => 15,
            Self::GrantExpired => 16,
            Self::GrantRevoked => 17,
            Self::RecordMismatch => 18,
            Self::LimitExceeded => 19,
            Self::InvalidValue => 20,
            Self::InvalidVmProfile => 21,
            Self::NoStateChange => 22,
        }
    }
}

impl PalaceGenesis {
    #[allow(clippy::too_many_arguments)]
    pub fn create(
        owner: AccountId,
        palace_id: StableId,
        title: String,
        active_manifest_cid: String,
        owner_profile: UserProfileInput,
        owner_grant_id: GrantId,
        entry_room_id: RoomId,
        entry_room: RoomConfigInput,
        secondary_room_id: RoomId,
        secondary_room: RoomConfigInput,
    ) -> Result<Self, PalaceError> {
        if !valid_nonzero_id(&owner) || !valid_nonzero_id(&palace_id) {
            return Err(PalaceError::InvalidIdentifier);
        }
        if !valid_text(&title, MAX_TITLE_BYTES) {
            return Err(PalaceError::InvalidText);
        }
        if !valid_cid(&active_manifest_cid) {
            return Err(PalaceError::InvalidCid);
        }
        if !valid_nonzero_id(&entry_room_id)
            || !valid_nonzero_id(&secondary_room_id)
            || entry_room_id == secondary_room_id
        {
            return Err(PalaceError::InvalidRooms);
        }
        if !valid_nonzero_id(&owner_grant_id) {
            return Err(PalaceError::InvalidIdentifier);
        }
        owner_profile.validate()?;
        entry_room.validate()?;
        secondary_room.validate()?;

        let root = PalaceRoot {
            record_type: RecordType::PalaceRoot,
            schema_version: SCHEMA_VERSION,
            palace_id,
            title,
            owner,
            entry_room_id,
            room_ids: [entry_room_id, secondary_room_id],
            active_manifest_cid,
            user_count: 1,
            grant_count: 1,
            ban_count: 0,
            shared_state_count: 0,
            revision: 0,
            last_ordered_action_id: 0,
        };
        let owner_profile = UserProfile {
            record_type: RecordType::UserProfile,
            schema_version: SCHEMA_VERSION,
            palace_id,
            user_id: owner,
            display_name: owner_profile.display_name,
            delivery_key: owner_profile.delivery_key,
            key_epoch: owner_profile.key_epoch,
            avatar_manifest_cid: owner_profile.avatar_manifest_cid,
            profile_revision: 0,
        };
        let rooms = [
            RoomRecord::new(palace_id, entry_room_id, entry_room),
            RoomRecord::new(palace_id, secondary_room_id, secondary_room),
        ];
        let owner_grant = CapabilityGrant {
            record_type: RecordType::CapabilityGrant,
            schema_version: SCHEMA_VERSION,
            palace_id,
            grant_id: owner_grant_id,
            subject_user_id: owner,
            issued_by: owner,
            scope: PalaceScope::Palace,
            capabilities: ALL_CAPABILITIES,
            delegable: true,
            valid_through_action_id: u64::MAX,
            revoked: false,
            revision: 0,
        };

        root.validate()?;
        owner_profile.validate()?;
        rooms[0].validate()?;
        rooms[1].validate()?;
        owner_grant.validate()?;

        Ok(Self {
            root,
            owner_profile,
            rooms,
            owner_grant,
        })
    }
}

impl PalaceRoot {
    pub fn validate(&self) -> Result<(), PalaceError> {
        validate_header(
            self.record_type,
            RecordType::PalaceRoot,
            self.schema_version,
        )?;
        if !valid_nonzero_id(&self.palace_id)
            || !valid_nonzero_id(&self.owner)
            || !valid_text(&self.title, MAX_TITLE_BYTES)
            || !valid_cid(&self.active_manifest_cid)
        {
            return Err(PalaceError::RecordMismatch);
        }
        if self.room_ids[0] == self.room_ids[1]
            || self.room_ids.iter().any(|id| !valid_nonzero_id(id))
            || !self.room_ids.contains(&self.entry_room_id)
        {
            return Err(PalaceError::InvalidRooms);
        }
        if self.user_count == 0
            || self.user_count > MAX_USERS
            || self.grant_count == 0
            || self.grant_count > MAX_GRANTS
            || self.ban_count > MAX_BANS
            || self.shared_state_count > MAX_SHARED_STATES
        {
            return Err(PalaceError::LimitExceeded);
        }
        Ok(())
    }

    pub fn register_user(
        &mut self,
        caller: AccountId,
        ordered_action_id: u64,
        input: UserProfileInput,
    ) -> Result<UserProfile, PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        if !valid_nonzero_id(&caller) {
            return Err(PalaceError::InvalidIdentifier);
        }
        input.validate()?;
        next_root.user_count = increment_bounded(self.user_count, MAX_USERS)?;
        let profile = UserProfile {
            record_type: RecordType::UserProfile,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            user_id: caller,
            display_name: input.display_name,
            delivery_key: input.delivery_key,
            key_epoch: input.key_epoch,
            avatar_manifest_cid: input.avatar_manifest_cid,
            profile_revision: 0,
        };
        profile.validate()?;
        next_root.validate()?;
        *self = next_root;
        Ok(profile)
    }

    /// Register a participant and issue the bounded ingress capability used
    /// by the MVP door. Moderation and room-edit capabilities remain explicit
    /// owner grants.
    pub fn register_user_with_default_grant(
        &mut self,
        caller: AccountId,
        ordered_action_id: u64,
        input: UserProfileInput,
    ) -> Result<(UserProfile, CapabilityGrant), PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        if !valid_nonzero_id(&caller) {
            return Err(PalaceError::InvalidIdentifier);
        }
        input.validate()?;
        next_root.user_count = increment_bounded(self.user_count, MAX_USERS)?;
        next_root.grant_count = increment_bounded(self.grant_count, MAX_GRANTS)?;
        let profile = UserProfile {
            record_type: RecordType::UserProfile,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            user_id: caller,
            display_name: input.display_name,
            delivery_key: input.delivery_key,
            key_epoch: input.key_epoch,
            avatar_manifest_cid: input.avatar_manifest_cid,
            profile_revision: 0,
        };
        profile.validate()?;
        let grant = CapabilityGrant {
            record_type: RecordType::CapabilityGrant,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            grant_id: caller,
            subject_user_id: caller,
            issued_by: self.owner,
            scope: PalaceScope::Room(self.entry_room_id),
            capabilities: CAP_WRITE_SHARED_STATE,
            delegable: false,
            valid_through_action_id: u64::MAX,
            revoked: false,
            revision: 0,
        };
        grant.validate()?;
        next_root.validate()?;
        *self = next_root;
        Ok((profile, grant))
    }

    pub fn update_user_profile(
        &mut self,
        caller: AccountId,
        profile: &mut UserProfile,
        ordered_action_id: u64,
        display_name: String,
        avatar_manifest_cid: Option<String>,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_profile(profile, caller)?;
        if !valid_text(&display_name, MAX_DISPLAY_NAME_BYTES) {
            return Err(PalaceError::InvalidText);
        }
        valid_optional_cid(&avatar_manifest_cid)?;
        let mut next_profile = profile.clone();
        next_profile.display_name = display_name;
        next_profile.avatar_manifest_cid = avatar_manifest_cid;
        next_profile.profile_revision = increment_revision(profile.profile_revision)?;
        next_profile.validate()?;
        *self = next_root;
        *profile = next_profile;
        Ok(())
    }

    pub fn rotate_delivery_key(
        &mut self,
        caller: AccountId,
        profile: &mut UserProfile,
        ordered_action_id: u64,
        delivery_key: DeliveryKey,
        key_epoch: u64,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_profile(profile, caller)?;
        if !valid_nonzero_id(&delivery_key) {
            return Err(PalaceError::InvalidDeliveryKey);
        }
        if key_epoch <= profile.key_epoch {
            return Err(PalaceError::KeyEpochNotAdvanced);
        }
        let mut next_profile = profile.clone();
        next_profile.delivery_key = delivery_key;
        next_profile.key_epoch = key_epoch;
        next_profile.profile_revision = increment_revision(profile.profile_revision)?;
        next_profile.validate()?;
        *self = next_root;
        *profile = next_profile;
        Ok(())
    }

    pub fn publish_manifest(
        &mut self,
        caller: AccountId,
        ordered_action_id: u64,
        cid: String,
    ) -> Result<(), PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        self.require_owner(caller)?;
        if !valid_cid(&cid) {
            return Err(PalaceError::InvalidCid);
        }
        next_root.active_manifest_cid = cid;
        next_root.validate()?;
        *self = next_root;
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn update_room(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        room: &mut RoomRecord,
        ordered_action_id: u64,
        grant_id: GrantId,
        room_id: RoomId,
        config: RoomConfigInput,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_room_record(room, room_id)?;
        self.require_capability(
            grant,
            caller,
            grant_id,
            &PalaceScope::Room(room_id),
            CAP_ROOM_EDIT,
            ordered_action_id,
        )?;
        config.validate()?;
        let mut next_room = room.clone();
        next_room.title = config.title;
        next_room.manifest_cid = config.manifest_cid;
        next_room.script_bundle_cid = config.script_bundle_cid;
        next_room.vm_profile = config.vm_profile;
        next_room.revision = increment_revision(room.revision)?;
        next_room.validate()?;
        *self = next_root;
        *room = next_room;
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn grant_capability(
        &mut self,
        caller: AccountId,
        subject_profile: &UserProfile,
        ordered_action_id: u64,
        grant_id: GrantId,
        scope: PalaceScope,
        capabilities: u32,
        delegable: bool,
        valid_through_action_id: u64,
    ) -> Result<CapabilityGrant, PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        self.require_owner(caller)?;
        self.require_profile(subject_profile, subject_profile.user_id)?;
        validate_scope(self, &scope)?;
        validate_capabilities(capabilities)?;
        if !valid_nonzero_id(&grant_id) {
            return Err(PalaceError::InvalidIdentifier);
        }
        if valid_through_action_id < ordered_action_id {
            return Err(PalaceError::GrantExpired);
        }
        next_root.grant_count = increment_bounded(self.grant_count, MAX_GRANTS)?;
        let grant = CapabilityGrant {
            record_type: RecordType::CapabilityGrant,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            grant_id,
            subject_user_id: subject_profile.user_id,
            issued_by: caller,
            scope,
            capabilities,
            delegable,
            valid_through_action_id,
            revoked: false,
            revision: 0,
        };
        grant.validate()?;
        next_root.validate()?;
        *self = next_root;
        Ok(grant)
    }

    pub fn revoke_capability(
        &mut self,
        caller: AccountId,
        grant: &mut CapabilityGrant,
        ordered_action_id: u64,
        grant_id: GrantId,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_owner(caller)?;
        self.require_grant_record(grant, grant_id)?;
        if grant.revoked {
            return Err(PalaceError::NoStateChange);
        }
        let mut next_grant = grant.clone();
        next_grant.revoked = true;
        next_grant.revision = increment_revision(grant.revision)?;
        next_grant.validate()?;
        *self = next_root;
        *grant = next_grant;
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn set_room_locked(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        room: &mut RoomRecord,
        ordered_action_id: u64,
        grant_id: GrantId,
        room_id: RoomId,
        locked: bool,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_room_record(room, room_id)?;
        self.require_capability(
            grant,
            caller,
            grant_id,
            &PalaceScope::Room(room_id),
            CAP_SET_ROOM_LOCK,
            ordered_action_id,
        )?;
        if room.locked == locked {
            return Err(PalaceError::NoStateChange);
        }
        let mut next_room = room.clone();
        next_room.locked = locked;
        next_room.revision = increment_revision(room.revision)?;
        next_room.validate()?;
        *self = next_root;
        *room = next_room;
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn create_user_ban(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        subject_profile: &UserProfile,
        ordered_action_id: u64,
        grant_id: GrantId,
        ban_id: BanId,
        scope: PalaceScope,
    ) -> Result<Ban, PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        self.require_profile(subject_profile, subject_profile.user_id)?;
        validate_scope(self, &scope)?;
        self.require_capability(
            grant,
            caller,
            grant_id,
            &scope,
            CAP_MODERATE_USER,
            ordered_action_id,
        )?;
        if subject_profile.user_id == self.owner {
            return Err(PalaceError::OwnerCannotBeBanned);
        }
        validate_new_record_id(&ban_id)?;
        next_root.ban_count = increment_bounded(self.ban_count, MAX_BANS)?;
        let ban = Ban {
            record_type: RecordType::Ban,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            ban_id,
            target: BanTarget::User(subject_profile.user_id),
            issuer: caller,
            scope,
            active: true,
            revision: 0,
        };
        ban.validate()?;
        next_root.validate()?;
        *self = next_root;
        Ok(ban)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn create_asset_ban(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        ordered_action_id: u64,
        grant_id: GrantId,
        ban_id: BanId,
        cid: String,
        scope: PalaceScope,
    ) -> Result<Ban, PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        validate_scope(self, &scope)?;
        self.require_capability(
            grant,
            caller,
            grant_id,
            &scope,
            CAP_MODERATE_ASSET,
            ordered_action_id,
        )?;
        validate_new_record_id(&ban_id)?;
        if !valid_cid(&cid) {
            return Err(PalaceError::InvalidCid);
        }
        next_root.ban_count = increment_bounded(self.ban_count, MAX_BANS)?;
        let ban = Ban {
            record_type: RecordType::Ban,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            ban_id,
            target: BanTarget::AssetCid(cid),
            issuer: caller,
            scope,
            active: true,
            revision: 0,
        };
        ban.validate()?;
        next_root.validate()?;
        *self = next_root;
        Ok(ban)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn set_ban_active(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        ban: &mut Ban,
        ordered_action_id: u64,
        grant_id: GrantId,
        ban_id: BanId,
        active: bool,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_ban_record(ban, ban_id)?;
        let capability = match ban.target {
            BanTarget::User(_) => CAP_MODERATE_USER,
            BanTarget::AssetCid(_) => CAP_MODERATE_ASSET,
        };
        self.require_capability(
            grant,
            caller,
            grant_id,
            &ban.scope,
            capability,
            ordered_action_id,
        )?;
        if ban.active == active {
            return Err(PalaceError::NoStateChange);
        }
        let mut next_ban = ban.clone();
        next_ban.active = active;
        next_ban.revision = increment_revision(ban.revision)?;
        next_ban.validate()?;
        *self = next_root;
        *ban = next_ban;
        Ok(())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn create_shared_state(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        room: &RoomRecord,
        ordered_action_id: u64,
        grant_id: GrantId,
        shared_state_id: SharedStateId,
        room_id: RoomId,
        key: String,
        value: Vec<u8>,
        state_root: StateRoot,
    ) -> Result<RoomSharedState, PalaceError> {
        let mut next_root = self.advanced(ordered_action_id)?;
        self.require_room_record(room, room_id)?;
        self.require_capability(
            grant,
            caller,
            grant_id,
            &PalaceScope::Room(room_id),
            CAP_WRITE_SHARED_STATE,
            ordered_action_id,
        )?;
        validate_new_record_id(&shared_state_id)?;
        validate_shared_value(&key, &value, &state_root)?;
        next_root.shared_state_count =
            increment_bounded(self.shared_state_count, MAX_SHARED_STATES)?;
        let shared = RoomSharedState {
            record_type: RecordType::RoomSharedState,
            schema_version: SCHEMA_VERSION,
            palace_id: self.palace_id,
            shared_state_id,
            room_id,
            key,
            value,
            state_root,
            revision: 1,
            last_ordered_action_id: ordered_action_id,
        };
        shared.validate()?;
        next_root.validate()?;
        *self = next_root;
        Ok(shared)
    }

    #[allow(clippy::too_many_arguments)]
    pub fn update_shared_state(
        &mut self,
        caller: AccountId,
        grant: &CapabilityGrant,
        room: &RoomRecord,
        shared: &mut RoomSharedState,
        ordered_action_id: u64,
        grant_id: GrantId,
        shared_state_id: SharedStateId,
        room_id: RoomId,
        state_revision: u64,
        value: Vec<u8>,
        state_root: StateRoot,
    ) -> Result<(), PalaceError> {
        let next_root = self.advanced(ordered_action_id)?;
        self.require_room_record(room, room_id)?;
        self.require_shared_record(shared, shared_state_id, room_id)?;
        self.require_capability(
            grant,
            caller,
            grant_id,
            &PalaceScope::Room(room_id),
            CAP_WRITE_SHARED_STATE,
            ordered_action_id,
        )?;
        let expected_revision = increment_revision(shared.revision)?;
        if state_revision != expected_revision {
            return Err(PalaceError::RecordRevisionOutOfSequence);
        }
        validate_shared_value(&shared.key, &value, &state_root)?;
        let mut next_shared = shared.clone();
        next_shared.value = value;
        next_shared.state_root = state_root;
        next_shared.revision = state_revision;
        next_shared.last_ordered_action_id = ordered_action_id;
        next_shared.validate()?;
        *self = next_root;
        *shared = next_shared;
        Ok(())
    }

    fn advanced(&self, ordered_action_id: u64) -> Result<Self, PalaceError> {
        self.validate()?;
        let expected = self
            .last_ordered_action_id
            .checked_add(1)
            .ok_or(PalaceError::OrderedActionIdOutOfSequence)?;
        if ordered_action_id != expected {
            return Err(PalaceError::OrderedActionIdOutOfSequence);
        }
        let mut next = self.clone();
        next.revision = increment_revision(self.revision)?;
        next.last_ordered_action_id = ordered_action_id;
        Ok(next)
    }

    fn require_owner(&self, caller: AccountId) -> Result<(), PalaceError> {
        if caller == self.owner {
            Ok(())
        } else {
            Err(PalaceError::Unauthorized)
        }
    }

    fn require_profile(
        &self,
        profile: &UserProfile,
        expected_user: AccountId,
    ) -> Result<(), PalaceError> {
        profile.validate()?;
        if profile.palace_id == self.palace_id && profile.user_id == expected_user {
            Ok(())
        } else {
            Err(PalaceError::RecordMismatch)
        }
    }

    fn require_room_record(
        &self,
        room: &RoomRecord,
        expected_room: RoomId,
    ) -> Result<(), PalaceError> {
        room.validate()?;
        if room.palace_id != self.palace_id || room.room_id != expected_room {
            return Err(PalaceError::RecordMismatch);
        }
        if self.room_ids.contains(&expected_room) {
            Ok(())
        } else {
            Err(PalaceError::UnknownRoom)
        }
    }

    fn require_grant_record(
        &self,
        grant: &CapabilityGrant,
        expected_grant: GrantId,
    ) -> Result<(), PalaceError> {
        grant.validate()?;
        if grant.palace_id == self.palace_id
            && grant.grant_id == expected_grant
            && grant.issued_by == self.owner
        {
            Ok(())
        } else {
            Err(PalaceError::RecordMismatch)
        }
    }

    fn require_ban_record(&self, ban: &Ban, expected_ban: BanId) -> Result<(), PalaceError> {
        ban.validate()?;
        if ban.palace_id != self.palace_id || ban.ban_id != expected_ban {
            Err(PalaceError::RecordMismatch)
        } else if matches!(ban.target, BanTarget::User(user_id) if user_id == self.owner) {
            Err(PalaceError::OwnerCannotBeBanned)
        } else {
            validate_scope(self, &ban.scope)
        }
    }

    fn require_shared_record(
        &self,
        shared: &RoomSharedState,
        expected_shared: SharedStateId,
        expected_room: RoomId,
    ) -> Result<(), PalaceError> {
        shared.validate()?;
        if shared.palace_id == self.palace_id
            && shared.shared_state_id == expected_shared
            && shared.room_id == expected_room
        {
            Ok(())
        } else {
            Err(PalaceError::RecordMismatch)
        }
    }

    fn require_capability(
        &self,
        grant: &CapabilityGrant,
        caller: AccountId,
        expected_grant: GrantId,
        requested_scope: &PalaceScope,
        capability: u32,
        ordered_action_id: u64,
    ) -> Result<(), PalaceError> {
        self.require_grant_record(grant, expected_grant)?;
        validate_scope(self, requested_scope)?;
        if grant.subject_user_id != caller {
            return Err(PalaceError::Unauthorized);
        }
        if grant.revoked {
            return Err(PalaceError::GrantRevoked);
        }
        if ordered_action_id > grant.valid_through_action_id {
            return Err(PalaceError::GrantExpired);
        }
        if !grant.scope.covers(requested_scope) || grant.capabilities & capability != capability {
            return Err(PalaceError::Unauthorized);
        }
        Ok(())
    }
}

impl UserProfile {
    pub fn validate(&self) -> Result<(), PalaceError> {
        validate_header(
            self.record_type,
            RecordType::UserProfile,
            self.schema_version,
        )?;
        if !valid_nonzero_id(&self.palace_id)
            || !valid_nonzero_id(&self.user_id)
            || !valid_text(&self.display_name, MAX_DISPLAY_NAME_BYTES)
            || !valid_nonzero_id(&self.delivery_key)
            || self.key_epoch == 0
        {
            return Err(PalaceError::RecordMismatch);
        }
        valid_optional_cid(&self.avatar_manifest_cid)
    }
}

impl RoomRecord {
    fn new(palace_id: StableId, room_id: RoomId, config: RoomConfigInput) -> Self {
        Self {
            record_type: RecordType::Room,
            schema_version: SCHEMA_VERSION,
            palace_id,
            room_id,
            title: config.title,
            manifest_cid: config.manifest_cid,
            script_bundle_cid: config.script_bundle_cid,
            vm_profile: config.vm_profile,
            locked: false,
            revision: 0,
        }
    }

    pub fn validate(&self) -> Result<(), PalaceError> {
        validate_header(self.record_type, RecordType::Room, self.schema_version)?;
        if !valid_nonzero_id(&self.palace_id)
            || !valid_nonzero_id(&self.room_id)
            || !valid_text(&self.title, MAX_TITLE_BYTES)
        {
            return Err(PalaceError::RecordMismatch);
        }
        if !valid_cid(&self.manifest_cid) || !valid_cid(&self.script_bundle_cid) {
            return Err(PalaceError::InvalidCid);
        }
        if self.vm_profile == VmProfile::NoScript {
            return Err(PalaceError::InvalidVmProfile);
        }
        Ok(())
    }
}

impl CapabilityGrant {
    pub fn validate(&self) -> Result<(), PalaceError> {
        validate_header(
            self.record_type,
            RecordType::CapabilityGrant,
            self.schema_version,
        )?;
        if !valid_nonzero_id(&self.palace_id)
            || !valid_nonzero_id(&self.grant_id)
            || !valid_nonzero_id(&self.subject_user_id)
            || !valid_nonzero_id(&self.issued_by)
        {
            return Err(PalaceError::RecordMismatch);
        }
        if self
            .scope
            .as_room()
            .is_some_and(|room_id| !valid_nonzero_id(room_id))
        {
            return Err(PalaceError::InvalidIdentifier);
        }
        if self.valid_through_action_id == 0 {
            return Err(PalaceError::InvalidValue);
        }
        validate_capabilities(self.capabilities)
    }
}

impl Ban {
    pub fn validate(&self) -> Result<(), PalaceError> {
        validate_header(self.record_type, RecordType::Ban, self.schema_version)?;
        if !valid_nonzero_id(&self.palace_id)
            || !valid_nonzero_id(&self.ban_id)
            || !valid_nonzero_id(&self.issuer)
        {
            return Err(PalaceError::RecordMismatch);
        }
        if self
            .scope
            .as_room()
            .is_some_and(|room_id| !valid_nonzero_id(room_id))
        {
            return Err(PalaceError::InvalidIdentifier);
        }
        match &self.target {
            BanTarget::User(user) if valid_nonzero_id(user) => Ok(()),
            BanTarget::AssetCid(cid) if valid_cid(cid) => Ok(()),
            BanTarget::User(_) => Err(PalaceError::InvalidIdentifier),
            BanTarget::AssetCid(_) => Err(PalaceError::InvalidCid),
        }
    }
}

impl RoomSharedState {
    pub fn validate(&self) -> Result<(), PalaceError> {
        validate_header(
            self.record_type,
            RecordType::RoomSharedState,
            self.schema_version,
        )?;
        if !valid_nonzero_id(&self.palace_id)
            || !valid_nonzero_id(&self.shared_state_id)
            || !valid_nonzero_id(&self.room_id)
            || self.revision == 0
            || self.last_ordered_action_id == 0
            || self.revision > self.last_ordered_action_id
        {
            return Err(PalaceError::RecordMismatch);
        }
        validate_shared_value(&self.key, &self.value, &self.state_root)
    }
}

fn validate_header(
    actual_type: RecordType,
    expected_type: RecordType,
    schema_version: u16,
) -> Result<(), PalaceError> {
    if schema_version != SCHEMA_VERSION {
        return Err(PalaceError::UnsupportedSchema);
    }
    if actual_type != expected_type {
        return Err(PalaceError::RecordMismatch);
    }
    Ok(())
}

fn validate_scope(root: &PalaceRoot, scope: &PalaceScope) -> Result<(), PalaceError> {
    match scope {
        PalaceScope::Palace => Ok(()),
        PalaceScope::Room(room_id) if root.room_ids.contains(room_id) => Ok(()),
        PalaceScope::Room(_) => Err(PalaceError::UnknownRoom),
    }
}

fn validate_capabilities(capabilities: u32) -> Result<(), PalaceError> {
    if capabilities == 0 || capabilities & !ALL_CAPABILITIES != 0 {
        Err(PalaceError::InvalidCapability)
    } else {
        Ok(())
    }
}

fn validate_new_record_id(id: &StableId) -> Result<(), PalaceError> {
    if valid_nonzero_id(id) {
        Ok(())
    } else {
        Err(PalaceError::InvalidIdentifier)
    }
}

fn validate_shared_value(
    key: &str,
    value: &[u8],
    state_root: &StateRoot,
) -> Result<(), PalaceError> {
    if !valid_ascii_identifier(key, MAX_SHARED_KEY_BYTES) {
        return Err(PalaceError::InvalidIdentifier);
    }
    if value.len() > MAX_SHARED_VALUE_BYTES || !valid_nonzero_id(state_root) {
        return Err(PalaceError::InvalidValue);
    }
    Ok(())
}

fn valid_nonzero_id(value: &[u8; 32]) -> bool {
    value.iter().any(|byte| *byte != 0)
}

fn valid_ascii_identifier(value: &str, max_bytes: usize) -> bool {
    !value.is_empty()
        && value.len() <= max_bytes
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'-')
}

fn valid_text(value: &str, max_bytes: usize) -> bool {
    !value.is_empty()
        && value.len() <= max_bytes
        && value.chars().all(|character| !character.is_control())
}

fn valid_cid(value: &str) -> bool {
    (4..=MAX_CID_BYTES).contains(&value.len())
        && value.bytes().all(|byte| byte.is_ascii_alphanumeric())
}

fn valid_optional_cid(value: &Option<String>) -> Result<(), PalaceError> {
    if value.as_ref().is_some_and(|cid| !valid_cid(cid)) {
        Err(PalaceError::InvalidCid)
    } else {
        Ok(())
    }
}

fn increment_revision(value: u64) -> Result<u64, PalaceError> {
    value.checked_add(1).ok_or(PalaceError::RevisionExhausted)
}

fn increment_bounded(value: u32, maximum: u32) -> Result<u32, PalaceError> {
    let next = value.checked_add(1).ok_or(PalaceError::LimitExceeded)?;
    if next > maximum {
        Err(PalaceError::LimitExceeded)
    } else {
        Ok(next)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const ALICE: AccountId = [1; 32];
    const BOB: AccountId = [2; 32];
    const CAROL: AccountId = [3; 32];
    const PALACE: StableId = [9; 32];
    const ATRIUM: RoomId = [10; 32];
    const LOUNGE: RoomId = [11; 32];
    const OWNER_GRANT: GrantId = [12; 32];
    const BOB_GRANT: GrantId = [13; 32];

    fn cid(suffix: char) -> String {
        format!("bafy{suffix}aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
    }

    fn profile(name: &str, key: u8) -> UserProfileInput {
        UserProfileInput {
            display_name: name.into(),
            delivery_key: [key; 32],
            key_epoch: 1,
            avatar_manifest_cid: Some(cid('a')),
        }
    }

    fn room(title: &str, suffix: char) -> RoomConfigInput {
        RoomConfigInput {
            title: title.into(),
            manifest_cid: cid(suffix),
            script_bundle_cid: cid('s'),
            vm_profile: VmProfile::IptScraeMvpV1,
        }
    }

    fn genesis() -> PalaceGenesis {
        PalaceGenesis::create(
            ALICE,
            PALACE,
            "Public Palace".into(),
            cid('p'),
            profile("Alice", 7),
            OWNER_GRANT,
            ATRIUM,
            room("Atrium", 'r'),
            LOUNGE,
            room("Lounge", 'l'),
        )
        .expect("valid genesis")
    }

    #[test]
    fn authorized_full_mvp_flow_updates_bounded_records() {
        let PalaceGenesis {
            mut root,
            mut rooms,
            owner_grant,
            ..
        } = genesis();
        let bob = root
            .register_user(BOB, 1, profile("Bob", 8))
            .expect("Bob registers");
        let carol = root
            .register_user(CAROL, 2, profile("Carol", 9))
            .expect("Carol registers");
        let bob_grant = root
            .grant_capability(
                ALICE,
                &bob,
                3,
                BOB_GRANT,
                PalaceScope::Palace,
                CAP_MODERATE_USER | CAP_MODERATE_ASSET | CAP_SET_ROOM_LOCK | CAP_WRITE_SHARED_STATE,
                false,
                100,
            )
            .expect("owner grants Bob moderation");
        let before_denied_edit = root.clone();
        let before_denied_room = rooms[0].clone();
        assert_eq!(
            root.update_room(
                BOB,
                &bob_grant,
                &mut rooms[0],
                4,
                BOB_GRANT,
                ATRIUM,
                room("Denied", 'd'),
            ),
            Err(PalaceError::Unauthorized)
        );
        assert_eq!(root, before_denied_edit);
        assert_eq!(rooms[0], before_denied_room);
        root.set_room_locked(BOB, &bob_grant, &mut rooms[1], 4, BOB_GRANT, LOUNGE, true)
            .expect("Bob locks room");
        let mut shared = root
            .create_shared_state(
                BOB,
                &bob_grant,
                &rooms[0],
                5,
                BOB_GRANT,
                [20; 32],
                ATRIUM,
                "door".into(),
                vec![0],
                [21; 32],
            )
            .expect("Bob writes explicit-cap shared state");
        root.update_shared_state(
            BOB,
            &bob_grant,
            &rooms[0],
            &mut shared,
            6,
            BOB_GRANT,
            [20; 32],
            ATRIUM,
            2,
            vec![1],
            [22; 32],
        )
        .expect("Bob advances shared state");
        let ban = root
            .create_user_ban(
                BOB,
                &bob_grant,
                &carol,
                7,
                BOB_GRANT,
                [30; 32],
                PalaceScope::Room(ATRIUM),
            )
            .expect("Bob bans Carol");
        root.publish_manifest(ALICE, 8, cid('n'))
            .expect("owner replaces active manifest");
        root.update_room(
            ALICE,
            &owner_grant,
            &mut rooms[0],
            9,
            OWNER_GRANT,
            ATRIUM,
            room("Atrium v2", 'v'),
        )
        .expect("owner publishes room through explicit room-edit authority");

        assert_eq!(root.last_ordered_action_id, 9);
        assert_eq!(root.revision, 9);
        assert_eq!(root.user_count, 3);
        assert_eq!(root.grant_count, 2);
        assert_eq!(root.ban_count, 1);
        assert_eq!(root.shared_state_count, 1);
        assert!(rooms[1].locked);
        assert_eq!(shared.revision, 2);
        assert_eq!(shared.last_ordered_action_id, 6);
        assert!(ban.active);
    }

    #[test]
    fn registration_issues_atrium_ingress_grant() {
        let mut root = genesis().root;
        let (profile, grant) = root
            .register_user_with_default_grant(BOB, 1, profile("Bob", 8))
            .expect("Bob registers with ingress grant");

        assert_eq!(profile.user_id, BOB);
        assert_eq!(grant.grant_id, BOB);
        assert_eq!(grant.subject_user_id, BOB);
        assert_eq!(grant.issued_by, ALICE);
        assert_eq!(grant.scope, PalaceScope::Room(ATRIUM));
        assert_eq!(grant.capabilities, CAP_WRITE_SHARED_STATE);
        assert!(!grant.delegable);
        assert!(!grant.revoked);
        assert_eq!(root.user_count, 2);
        assert_eq!(root.grant_count, 2);
        assert_eq!(root.last_ordered_action_id, 1);
    }

    #[test]
    fn unauthorized_and_missing_capability_actions_consume_nothing() {
        let PalaceGenesis {
            mut root,
            owner_grant,
            mut rooms,
            ..
        } = genesis();
        let before_root = root.clone();
        let before_room = rooms[1].clone();

        assert_eq!(
            root.set_room_locked(
                BOB,
                &owner_grant,
                &mut rooms[1],
                1,
                OWNER_GRANT,
                LOUNGE,
                true,
            ),
            Err(PalaceError::Unauthorized)
        );
        assert_eq!(root, before_root);
        assert_eq!(rooms[1], before_room);
        assert_eq!(
            root.publish_manifest(BOB, 1, cid('x')),
            Err(PalaceError::Unauthorized)
        );
        assert_eq!(root, before_root);
    }

    #[test]
    fn expired_and_revoked_grants_fail_without_consuming_actions() {
        let PalaceGenesis {
            mut root,
            mut rooms,
            ..
        } = genesis();
        let bob = root
            .register_user(BOB, 1, profile("Bob", 8))
            .expect("Bob registers");
        let expired = root
            .grant_capability(
                ALICE,
                &bob,
                2,
                BOB_GRANT,
                PalaceScope::Palace,
                CAP_SET_ROOM_LOCK,
                false,
                2,
            )
            .expect("grant is valid through its creation action");
        let before_expired = root.clone();
        assert_eq!(
            root.set_room_locked(BOB, &expired, &mut rooms[1], 3, BOB_GRANT, LOUNGE, true,),
            Err(PalaceError::GrantExpired)
        );
        assert_eq!(root, before_expired);

        let active_grant_id = [14; 32];
        let mut revoked = root
            .grant_capability(
                ALICE,
                &bob,
                3,
                active_grant_id,
                PalaceScope::Palace,
                CAP_SET_ROOM_LOCK,
                false,
                100,
            )
            .expect("replacement grant");
        root.revoke_capability(ALICE, &mut revoked, 4, active_grant_id)
            .expect("owner revokes grant");
        let before_revoked = root.clone();
        assert_eq!(
            root.set_room_locked(
                BOB,
                &revoked,
                &mut rooms[1],
                5,
                active_grant_id,
                LOUNGE,
                true,
            ),
            Err(PalaceError::GrantRevoked)
        );
        assert_eq!(root, before_revoked);
        assert!(!rooms[1].locked);
    }

    #[test]
    fn record_type_and_identity_confusion_fail_closed() {
        let PalaceGenesis {
            mut root,
            owner_profile,
            owner_grant,
            mut rooms,
            ..
        } = genesis();
        let mut wrong_room = rooms[0].clone();
        wrong_room.room_id = LOUNGE;
        let root_before = root.clone();

        assert_eq!(
            root.update_room(
                ALICE,
                &owner_grant,
                &mut wrong_room,
                1,
                OWNER_GRANT,
                ATRIUM,
                room("Wrong", 'w')
            ),
            Err(PalaceError::RecordMismatch)
        );
        assert_eq!(root, root_before);

        let profile_bytes = borsh::to_vec(&owner_profile).expect("profile fixture");
        let confused = RoomRecord::try_from_slice(&profile_bytes);
        assert!(confused.is_err() || confused.is_ok_and(|record| record.validate().is_err()));

        rooms[0].record_type = RecordType::Ban;
        assert_eq!(rooms[0].validate(), Err(PalaceError::RecordMismatch));
    }

    #[test]
    fn caps_bounds_and_ascii_identifiers_are_absolute() {
        let PalaceGenesis {
            mut root,
            owner_profile,
            owner_grant,
            rooms,
        } = genesis();
        assert_eq!(
            root.grant_capability(
                ALICE,
                &owner_profile,
                1,
                [44; 32],
                PalaceScope::Palace,
                ALL_CAPABILITIES | (1 << 31),
                false,
                10,
            ),
            Err(PalaceError::InvalidCapability)
        );
        assert_eq!(
            root.create_shared_state(
                ALICE,
                &owner_grant,
                &rooms[0],
                1,
                OWNER_GRANT,
                [45; 32],
                ATRIUM,
                "not/ascii/id".into(),
                vec![],
                [46; 32],
            ),
            Err(PalaceError::InvalidIdentifier)
        );
        assert_eq!(
            root.create_shared_state(
                ALICE,
                &owner_grant,
                &rooms[0],
                1,
                OWNER_GRANT,
                [45; 32],
                ATRIUM,
                "door".into(),
                vec![0; MAX_SHARED_VALUE_BYTES + 1],
                [46; 32],
            ),
            Err(PalaceError::InvalidValue)
        );
        assert_eq!(root.last_ordered_action_id, 0);

        root.user_count = MAX_USERS;
        assert_eq!(
            root.register_user(BOB, 1, profile("Bob", 8)),
            Err(PalaceError::LimitExceeded)
        );

        let PalaceGenesis {
            mut root,
            owner_profile,
            ..
        } = genesis();
        root.grant_count = MAX_GRANTS;
        assert_eq!(
            root.grant_capability(
                ALICE,
                &owner_profile,
                1,
                [47; 32],
                PalaceScope::Palace,
                CAP_MODERATE_USER,
                false,
                10,
            ),
            Err(PalaceError::LimitExceeded)
        );

        let PalaceGenesis {
            mut root,
            owner_grant,
            rooms,
            ..
        } = genesis();
        root.ban_count = MAX_BANS;
        assert_eq!(
            root.create_asset_ban(
                ALICE,
                &owner_grant,
                1,
                OWNER_GRANT,
                [48; 32],
                cid('b'),
                PalaceScope::Palace,
            ),
            Err(PalaceError::LimitExceeded)
        );
        root.ban_count = 0;
        root.shared_state_count = MAX_SHARED_STATES;
        assert_eq!(
            root.create_shared_state(
                ALICE,
                &owner_grant,
                &rooms[0],
                1,
                OWNER_GRANT,
                [49; 32],
                ATRIUM,
                "door".into(),
                vec![],
                [50; 32],
            ),
            Err(PalaceError::LimitExceeded)
        );
    }

    #[test]
    fn ordered_actions_reject_gaps_replays_and_overflow_without_mutation() {
        let mut root = genesis().root;
        let initial = root.clone();
        assert_eq!(
            root.publish_manifest(ALICE, 2, cid('x')),
            Err(PalaceError::OrderedActionIdOutOfSequence)
        );
        assert_eq!(root, initial);
        root.publish_manifest(ALICE, 1, cid('x'))
            .expect("first action");
        let after_first = root.clone();
        assert_eq!(
            root.publish_manifest(ALICE, 1, cid('y')),
            Err(PalaceError::OrderedActionIdOutOfSequence)
        );
        assert_eq!(root, after_first);

        root.last_ordered_action_id = u64::MAX;
        let exhausted = root.clone();
        assert_eq!(
            root.publish_manifest(ALICE, u64::MAX, cid('z')),
            Err(PalaceError::OrderedActionIdOutOfSequence)
        );
        assert_eq!(root, exhausted);

        let mut root = genesis().root;
        root.revision = u64::MAX;
        let exhausted = root.clone();
        assert_eq!(
            root.publish_manifest(ALICE, 1, cid('z')),
            Err(PalaceError::RevisionExhausted)
        );
        assert_eq!(root, exhausted);
    }

    #[test]
    fn all_v3_records_round_trip_with_borsh() {
        let mut genesis = genesis();
        let shared = genesis
            .root
            .create_shared_state(
                ALICE,
                &genesis.owner_grant,
                &genesis.rooms[0],
                1,
                OWNER_GRANT,
                [50; 32],
                ATRIUM,
                "door".into(),
                vec![1, 2, 3],
                [51; 32],
            )
            .expect("shared record");
        let ban = genesis
            .root
            .create_asset_ban(
                ALICE,
                &genesis.owner_grant,
                2,
                OWNER_GRANT,
                [52; 32],
                cid('b'),
                PalaceScope::Palace,
            )
            .expect("ban record");
        let records = [
            borsh::to_vec(&genesis.root).expect("root"),
            borsh::to_vec(&genesis.owner_profile).expect("profile"),
            borsh::to_vec(&genesis.rooms[0]).expect("room"),
            borsh::to_vec(&genesis.owner_grant).expect("grant"),
            borsh::to_vec(&ban).expect("ban"),
            borsh::to_vec(&shared).expect("shared"),
        ];
        assert_eq!(
            PalaceRoot::try_from_slice(&records[0]).expect("root decode"),
            genesis.root
        );
        assert_eq!(
            UserProfile::try_from_slice(&records[1]).expect("profile decode"),
            genesis.owner_profile
        );
        assert_eq!(
            RoomRecord::try_from_slice(&records[2]).expect("room decode"),
            genesis.rooms[0]
        );
        assert_eq!(
            CapabilityGrant::try_from_slice(&records[3]).expect("grant decode"),
            genesis.owner_grant
        );
        assert_eq!(Ban::try_from_slice(&records[4]).expect("ban decode"), ban);
        assert_eq!(
            RoomSharedState::try_from_slice(&records[5]).expect("shared decode"),
            shared
        );
        assert_eq!(SCHEMA_VERSION, 3);
    }

    #[test]
    fn invalid_enum_discriminants_do_not_deserialize() {
        let mut room_bytes = borsh::to_vec(&genesis().rooms[0]).expect("room");
        room_bytes[0] = u8::MAX;
        assert!(RoomRecord::try_from_slice(&room_bytes).is_err());
    }

    #[test]
    fn schema_v2_record_headers_are_rejected_explicitly() {
        let mut legacy_root = genesis().root;
        legacy_root.schema_version = 2;
        let bytes = borsh::to_vec(&legacy_root).expect("legacy-version fixture");
        let decoded = PalaceRoot::try_from_slice(&bytes).expect("layout remains decodable");
        assert_eq!(decoded.validate(), Err(PalaceError::UnsupportedSchema));
    }

    #[test]
    fn guest_rejection_codes_remain_unique_and_contiguous() {
        let errors = [
            PalaceError::UnsupportedSchema,
            PalaceError::InvalidRooms,
            PalaceError::InvalidIdentifier,
            PalaceError::InvalidCid,
            PalaceError::InvalidDeliveryKey,
            PalaceError::Unauthorized,
            PalaceError::UnknownUser,
            PalaceError::UnknownRoom,
            PalaceError::KeyEpochNotAdvanced,
            PalaceError::OwnerCannotBeBanned,
            PalaceError::RecordRevisionOutOfSequence,
            PalaceError::RevisionExhausted,
            PalaceError::OrderedActionIdOutOfSequence,
            PalaceError::InvalidText,
            PalaceError::InvalidCapability,
            PalaceError::GrantExpired,
            PalaceError::GrantRevoked,
            PalaceError::RecordMismatch,
            PalaceError::LimitExceeded,
            PalaceError::InvalidValue,
            PalaceError::InvalidVmProfile,
            PalaceError::NoStateChange,
        ];
        let codes: Vec<u32> = errors.iter().map(PalaceError::code).collect();
        assert_eq!(codes, (1..=22).collect::<Vec<_>>());
    }
}
