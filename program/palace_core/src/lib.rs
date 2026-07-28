//! Versioned state and instruction contract for the public two-room Palace.
//!
//! This crate has no network, wallet, filesystem, or UI dependency. The LEZ
//! guest owns account authorization; this deterministic core validates Palace
//! transition rules after the authenticated caller is known.

use std::collections::{BTreeMap, BTreeSet};

use borsh::{BorshDeserialize, BorshSerialize};
use serde::{Deserialize, Serialize};

pub const SCHEMA_VERSION: u16 = 2;
pub type AccountId = [u8; 32];
pub type DeliveryKey = [u8; 32];

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct Room {
    pub id: String,
    pub title: String,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct UserRecord {
    pub delivery_key: DeliveryKey,
    pub key_epoch: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub struct PalaceState {
    pub schema_version: u16,
    pub owner: AccountId,
    pub rooms: Vec<Room>,
    pub users: BTreeMap<AccountId, UserRecord>,
    pub moderators: BTreeSet<AccountId>,
    pub locked_rooms: BTreeSet<String>,
    pub user_bans: BTreeSet<(AccountId, String)>,
    pub asset_bans: BTreeSet<(String, String)>,
    pub manifests: BTreeSet<String>,
    pub shared_spot_revisions: BTreeMap<(String, String), u64>,
    pub revision: u64,
    pub last_ordered_action_id: u64,
}

#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub enum PalaceInstruction {
    BindDeliveryKey {
        subject: AccountId,
        delivery_key: DeliveryKey,
        key_epoch: u64,
    },
    DelegateModerator {
        subject: AccountId,
    },
    RevokeModerator {
        subject: AccountId,
    },
    BanUser {
        subject: AccountId,
        room_id: String,
    },
    BanAsset {
        cid: String,
        room_id: String,
    },
    SetRoomLocked {
        room_id: String,
        locked: bool,
    },
    PublishManifest {
        cid: String,
    },
    SetSharedSpotRevision {
        room_id: String,
        spot_id: String,
        revision: u64,
    },
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
    SpotRevisionNotAdvanced,
    RevisionExhausted,
    OrderedActionIdOutOfSequence,
}

impl PalaceError {
    /// Stable Palace-specific rejection code for LEZ guest and client handling.
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
            Self::SpotRevisionNotAdvanced => 11,
            Self::RevisionExhausted => 12,
            Self::OrderedActionIdOutOfSequence => 13,
        }
    }
}

impl PalaceState {
    pub fn create(
        owner: AccountId,
        owner_delivery_key: DeliveryKey,
        rooms: Vec<Room>,
    ) -> Result<Self, PalaceError> {
        if !valid_rooms(&rooms) {
            return Err(PalaceError::InvalidRooms);
        }
        if !valid_delivery_key(&owner_delivery_key) {
            return Err(PalaceError::InvalidDeliveryKey);
        }
        let mut users = BTreeMap::new();
        users.insert(
            owner,
            UserRecord {
                delivery_key: owner_delivery_key,
                key_epoch: 0,
            },
        );
        Ok(Self {
            schema_version: SCHEMA_VERSION,
            owner,
            rooms,
            users,
            moderators: BTreeSet::new(),
            locked_rooms: BTreeSet::new(),
            user_bans: BTreeSet::new(),
            asset_bans: BTreeSet::new(),
            manifests: BTreeSet::new(),
            shared_spot_revisions: BTreeMap::new(),
            revision: 0,
            last_ordered_action_id: 0,
        })
    }

    /// Apply exactly the next ordered action.
    ///
    /// Failed transitions leave both revision counters unchanged, so the same
    /// ordered action ID may be retried with a corrected transition.
    pub fn apply(
        &mut self,
        caller: AccountId,
        ordered_action_id: u64,
        instruction: PalaceInstruction,
    ) -> Result<(), PalaceError> {
        if self.schema_version != SCHEMA_VERSION {
            return Err(PalaceError::UnsupportedSchema);
        }
        let next_revision = self
            .revision
            .checked_add(1)
            .ok_or(PalaceError::RevisionExhausted)?;
        let next_ordered_action_id = self
            .last_ordered_action_id
            .checked_add(1)
            .ok_or(PalaceError::OrderedActionIdOutOfSequence)?;
        if ordered_action_id != next_ordered_action_id {
            return Err(PalaceError::OrderedActionIdOutOfSequence);
        }

        match instruction {
            PalaceInstruction::BindDeliveryKey {
                subject,
                delivery_key,
                key_epoch,
            } => {
                if caller != subject {
                    return Err(PalaceError::Unauthorized);
                }
                if !valid_delivery_key(&delivery_key) {
                    return Err(PalaceError::InvalidDeliveryKey);
                }
                if self
                    .users
                    .get(&subject)
                    .is_some_and(|current| key_epoch <= current.key_epoch)
                {
                    return Err(PalaceError::KeyEpochNotAdvanced);
                }
                self.users.insert(
                    subject,
                    UserRecord {
                        delivery_key,
                        key_epoch,
                    },
                );
            }
            PalaceInstruction::DelegateModerator { subject } => {
                self.require_owner(caller)?;
                self.require_user(subject)?;
                self.moderators.insert(subject);
            }
            PalaceInstruction::RevokeModerator { subject } => {
                self.require_owner(caller)?;
                self.moderators.remove(&subject);
            }
            PalaceInstruction::BanUser { subject, room_id } => {
                self.require_moderator(caller)?;
                self.require_room(&room_id)?;
                self.require_user(subject)?;
                if subject == self.owner {
                    return Err(PalaceError::OwnerCannotBeBanned);
                }
                self.user_bans.insert((subject, room_id));
            }
            PalaceInstruction::BanAsset { cid, room_id } => {
                self.require_moderator(caller)?;
                self.require_room(&room_id)?;
                if !valid_cid(&cid) {
                    return Err(PalaceError::InvalidCid);
                }
                self.asset_bans.insert((cid, room_id));
            }
            PalaceInstruction::SetRoomLocked { room_id, locked } => {
                self.require_moderator(caller)?;
                self.require_room(&room_id)?;
                if locked {
                    self.locked_rooms.insert(room_id);
                } else {
                    self.locked_rooms.remove(&room_id);
                }
            }
            PalaceInstruction::PublishManifest { cid } => {
                self.require_owner(caller)?;
                if !valid_cid(&cid) {
                    return Err(PalaceError::InvalidCid);
                }
                self.manifests.insert(cid);
            }
            PalaceInstruction::SetSharedSpotRevision {
                room_id,
                spot_id,
                revision,
            } => {
                self.require_owner(caller)?;
                self.require_room(&room_id)?;
                if !valid_identifier(&spot_id) {
                    return Err(PalaceError::InvalidIdentifier);
                }
                if revision == 0 {
                    return Err(PalaceError::SpotRevisionNotAdvanced);
                }
                let key = (room_id, spot_id);
                if self
                    .shared_spot_revisions
                    .get(&key)
                    .is_some_and(|current| revision <= *current)
                {
                    return Err(PalaceError::SpotRevisionNotAdvanced);
                }
                self.shared_spot_revisions.insert(key, revision);
            }
        }
        self.revision = next_revision;
        self.last_ordered_action_id = ordered_action_id;
        Ok(())
    }

    fn require_owner(&self, caller: AccountId) -> Result<(), PalaceError> {
        if caller == self.owner {
            Ok(())
        } else {
            Err(PalaceError::Unauthorized)
        }
    }

    fn require_moderator(&self, caller: AccountId) -> Result<(), PalaceError> {
        if caller == self.owner || self.moderators.contains(&caller) {
            Ok(())
        } else {
            Err(PalaceError::Unauthorized)
        }
    }

    fn require_user(&self, subject: AccountId) -> Result<(), PalaceError> {
        if self.users.contains_key(&subject) {
            Ok(())
        } else {
            Err(PalaceError::UnknownUser)
        }
    }

    fn require_room(&self, room_id: &str) -> Result<(), PalaceError> {
        if self.rooms.iter().any(|room| room.id == room_id) {
            Ok(())
        } else {
            Err(PalaceError::UnknownRoom)
        }
    }
}

fn valid_rooms(rooms: &[Room]) -> bool {
    if rooms.len() != 2 {
        return false;
    }
    let mut ids = BTreeSet::new();
    rooms.iter().all(|room| {
        valid_identifier(&room.id) && valid_text(&room.title, 64) && ids.insert(room.id.as_str())
    })
}

fn valid_identifier(value: &str) -> bool {
    !value.is_empty()
        && value.len() <= 64
        && value
            .bytes()
            .all(|byte| byte.is_ascii_alphanumeric() || byte == b'_' || byte == b'-')
}

fn valid_text(value: &str, limit: usize) -> bool {
    !value.is_empty()
        && value.len() <= limit
        && value.bytes().all(|byte| byte >= b' ' && byte != 0x7f)
}

fn valid_cid(value: &str) -> bool {
    value.len() >= 2 && value.len() <= 128 && value.bytes().all(|byte| byte.is_ascii_alphanumeric())
}

fn valid_delivery_key(value: &DeliveryKey) -> bool {
    value.iter().any(|byte| *byte != 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    const ALICE: AccountId = [1; 32];
    const BOB: AccountId = [2; 32];
    const CAROL: AccountId = [3; 32];

    fn state() -> PalaceState {
        PalaceState::create(
            ALICE,
            [9; 32],
            vec![
                Room {
                    id: "atrium".into(),
                    title: "Atrium".into(),
                },
                Room {
                    id: "lounge".into(),
                    title: "Lounge".into(),
                },
            ],
        )
        .expect("two public rooms are valid")
    }

    #[test]
    fn owner_binds_user_then_delegates_moderation() {
        let mut state = state();
        assert_eq!(
            state.apply(
                BOB,
                1,
                PalaceInstruction::BindDeliveryKey {
                    subject: BOB,
                    delivery_key: [8; 32],
                    key_epoch: 1,
                },
            ),
            Ok(())
        );
        assert_eq!(
            state.apply(
                ALICE,
                2,
                PalaceInstruction::DelegateModerator { subject: BOB }
            ),
            Ok(())
        );
        assert_eq!(
            state.apply(
                BOB,
                3,
                PalaceInstruction::SetRoomLocked {
                    room_id: "lounge".into(),
                    locked: true,
                },
            ),
            Ok(())
        );
        assert!(state.locked_rooms.contains("lounge"));
        assert_eq!(state.revision, 3);
        assert_eq!(state.last_ordered_action_id, 3);
    }

    #[test]
    fn raw_unauthorized_actions_do_not_mutate_state() {
        let mut state = state();
        assert_eq!(
            state.apply(
                CAROL,
                1,
                PalaceInstruction::BanUser {
                    subject: BOB,
                    room_id: "atrium".into(),
                },
            ),
            Err(PalaceError::Unauthorized)
        );
        assert!(state.user_bans.is_empty());
        assert_eq!(state.revision, 0);
        assert_eq!(state.last_ordered_action_id, 0);
    }

    #[test]
    fn key_and_shared_spot_revisions_only_advance() {
        let mut state = state();
        assert_eq!(
            state.apply(
                BOB,
                1,
                PalaceInstruction::BindDeliveryKey {
                    subject: BOB,
                    delivery_key: [8; 32],
                    key_epoch: 1,
                },
            ),
            Ok(())
        );
        assert_eq!(
            state.apply(
                BOB,
                2,
                PalaceInstruction::BindDeliveryKey {
                    subject: BOB,
                    delivery_key: [7; 32],
                    key_epoch: 1,
                },
            ),
            Err(PalaceError::KeyEpochNotAdvanced)
        );
        assert_eq!(
            state.apply(
                ALICE,
                2,
                PalaceInstruction::SetSharedSpotRevision {
                    room_id: "atrium".into(),
                    spot_id: "door".into(),
                    revision: 1,
                },
            ),
            Ok(())
        );
        assert_eq!(
            state.apply(
                ALICE,
                3,
                PalaceInstruction::SetSharedSpotRevision {
                    room_id: "atrium".into(),
                    spot_id: "door".into(),
                    revision: 1,
                },
            ),
            Err(PalaceError::SpotRevisionNotAdvanced)
        );
    }

    #[test]
    fn palace_creation_requires_exactly_two_distinct_public_rooms() {
        let rooms = vec![
            Room {
                id: "atrium".into(),
                title: "Atrium".into(),
            },
            Room {
                id: "atrium".into(),
                title: "Duplicate".into(),
            },
        ];
        assert_eq!(
            PalaceState::create(ALICE, [9; 32], rooms),
            Err(PalaceError::InvalidRooms)
        );
    }

    #[test]
    fn unsupported_state_or_zero_delivery_key_fails_closed() {
        assert_eq!(
            PalaceState::create(
                ALICE,
                [0; 32],
                vec![
                    Room {
                        id: "atrium".into(),
                        title: "Atrium".into(),
                    },
                    Room {
                        id: "lounge".into(),
                        title: "Lounge".into(),
                    },
                ],
            ),
            Err(PalaceError::InvalidDeliveryKey)
        );

        let mut state = state();
        state.schema_version = SCHEMA_VERSION + 1;
        assert_eq!(
            state.apply(
                ALICE,
                1,
                PalaceInstruction::PublishManifest { cid: "ba".into() }
            ),
            Err(PalaceError::UnsupportedSchema)
        );
        assert_eq!(state.revision, 0);
        assert_eq!(state.last_ordered_action_id, 0);
    }

    #[test]
    fn ordered_action_ids_reject_replays_gaps_and_competing_transitions() {
        let mut state = state();
        let initial = state.clone();

        assert_eq!(
            state.apply(
                ALICE,
                2,
                PalaceInstruction::PublishManifest { cid: "bafy".into() }
            ),
            Err(PalaceError::OrderedActionIdOutOfSequence)
        );
        assert_eq!(state, initial);

        assert_eq!(
            state.apply(
                ALICE,
                1,
                PalaceInstruction::PublishManifest { cid: "bafy".into() }
            ),
            Ok(())
        );
        let after_first = state.clone();

        assert_eq!(
            state.apply(
                ALICE,
                1,
                PalaceInstruction::PublishManifest {
                    cid: "bafysecond".into()
                }
            ),
            Err(PalaceError::OrderedActionIdOutOfSequence)
        );
        assert_eq!(state, after_first);
        assert_eq!(state.revision, 1);
        assert_eq!(state.last_ordered_action_id, 1);
        assert!(!state.manifests.contains("bafysecond"));
    }

    #[test]
    fn rejected_transition_does_not_consume_the_ordered_action_id() {
        let mut state = state();
        let initial = state.clone();

        assert_eq!(
            state.apply(
                ALICE,
                1,
                PalaceInstruction::PublishManifest { cid: "!".into() }
            ),
            Err(PalaceError::InvalidCid)
        );
        assert_eq!(state, initial);

        assert_eq!(
            state.apply(
                ALICE,
                1,
                PalaceInstruction::PublishManifest { cid: "bafy".into() }
            ),
            Ok(())
        );
        assert_eq!(state.revision, 1);
        assert_eq!(state.last_ordered_action_id, 1);
    }

    #[test]
    fn ordered_action_id_overflow_fails_closed() {
        let mut state = state();
        state.last_ordered_action_id = u64::MAX;
        let exhausted = state.clone();

        assert_eq!(
            state.apply(
                ALICE,
                u64::MAX,
                PalaceInstruction::PublishManifest { cid: "bafy".into() }
            ),
            Err(PalaceError::OrderedActionIdOutOfSequence)
        );
        assert_eq!(state, exhausted);
        assert_eq!(PalaceError::OrderedActionIdOutOfSequence.code(), 13);
    }

    #[test]
    fn schema_v2_state_bytes_reject_the_v1_layout() {
        #[derive(BorshSerialize)]
        struct PalaceStateV1 {
            schema_version: u16,
            owner: AccountId,
            rooms: Vec<Room>,
            users: BTreeMap<AccountId, UserRecord>,
            moderators: BTreeSet<AccountId>,
            locked_rooms: BTreeSet<String>,
            user_bans: BTreeSet<(AccountId, String)>,
            asset_bans: BTreeSet<(String, String)>,
            manifests: BTreeSet<String>,
            shared_spot_revisions: BTreeMap<(String, String), u64>,
            revision: u64,
        }

        let state = state();
        let legacy = PalaceStateV1 {
            schema_version: 1,
            owner: state.owner,
            rooms: state.rooms.clone(),
            users: state.users.clone(),
            moderators: state.moderators.clone(),
            locked_rooms: state.locked_rooms.clone(),
            user_bans: state.user_bans.clone(),
            asset_bans: state.asset_bans.clone(),
            manifests: state.manifests.clone(),
            shared_spot_revisions: state.shared_spot_revisions.clone(),
            revision: state.revision,
        };
        let v1_bytes = borsh::to_vec(&legacy).expect("schema v1 fixture serializes");
        assert!(PalaceState::try_from_slice(&v1_bytes).is_err());

        let v2_bytes = borsh::to_vec(&state).expect("schema v2 state serializes");
        let decoded =
            PalaceState::try_from_slice(&v2_bytes).expect("schema v2 state must deserialize");
        assert_eq!(decoded, state);
        assert_eq!(decoded.schema_version, 2);
        assert_eq!(decoded.last_ordered_action_id, 0);
    }
}
