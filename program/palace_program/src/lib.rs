//! Public Palace LEZ instruction schema.
//!
//! Every ordered instruction takes the canonical `PalaceRoot` as account zero.
//! Account ordering for each variant is documented on the variant and is part
//! of the client integration contract.

use borsh::{BorshDeserialize, BorshSerialize};
use serde::{Deserialize, Serialize};

pub use palace_program_core as core;

/// Versioned Palace instruction envelope consumed by the SPEL guest.
///
/// Schema v3 intentionally replaces the schema-v2 monolithic state account
/// with independently addressed, discriminated records.
#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub enum GuestInstruction {
    /// Accounts: root, owner signer, owner profile, entry room, secondary
    /// room, owner capability grant.
    Initialize {
        palace_id: core::StableId,
        title: String,
        active_manifest_cid: String,
        owner_profile: Box<core::UserProfileInput>,
        owner_grant_id: core::GrantId,
        entry_room_id: core::RoomId,
        entry_room: Box<core::RoomConfigInput>,
        secondary_room_id: core::RoomId,
        secondary_room: Box<core::RoomConfigInput>,
    },
    /// Accounts: root, registering user signer, new user profile.
    RegisterUser {
        ordered_action_id: u64,
        profile: core::UserProfileInput,
    },
    /// Accounts: root, profile owner signer, user profile.
    UpdateUserProfile {
        ordered_action_id: u64,
        display_name: String,
        avatar_manifest_cid: Option<String>,
    },
    /// Accounts: root, profile owner signer, user profile.
    RotateDeliveryKey {
        ordered_action_id: u64,
        delivery_key: core::DeliveryKey,
        key_epoch: u64,
    },
    /// Accounts: root, Palace owner signer.
    PublishManifest { ordered_action_id: u64, cid: String },
    /// Accounts: root, caller signer, room-edit grant, room.
    UpdateRoom {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        room_id: core::RoomId,
        config: core::RoomConfigInput,
    },
    /// Accounts: root, Palace owner signer, subject profile, new grant.
    GrantCapability {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        subject_user_id: core::AccountId,
        scope: core::PalaceScope,
        capabilities: u32,
        delegable: bool,
        valid_through_action_id: u64,
    },
    /// Accounts: root, Palace owner signer, grant.
    RevokeCapability {
        ordered_action_id: u64,
        grant_id: core::GrantId,
    },
    /// Accounts: root, caller signer, authority grant, room.
    SetRoomLocked {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        room_id: core::RoomId,
        locked: bool,
    },
    /// Accounts: root, caller signer, authority grant, subject profile, new ban.
    CreateUserBan {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        ban_id: core::BanId,
        subject_user_id: core::AccountId,
        scope: core::PalaceScope,
    },
    /// Accounts: root, caller signer, authority grant, new ban.
    CreateAssetBan {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        ban_id: core::BanId,
        cid: String,
        scope: core::PalaceScope,
    },
    /// Accounts: root, caller signer, authority grant, ban.
    SetBanActive {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        ban_id: core::BanId,
        active: bool,
    },
    /// Accounts: root, caller signer, authority grant, room, new shared state.
    CreateSharedState {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        shared_state_id: core::SharedStateId,
        room_id: core::RoomId,
        key: String,
        value: Vec<u8>,
        state_root: core::StateRoot,
    },
    /// Accounts: root, caller signer, authority grant, room, shared state.
    UpdateSharedState {
        ordered_action_id: u64,
        grant_id: core::GrantId,
        shared_state_id: core::SharedStateId,
        room_id: core::RoomId,
        state_revision: u64,
        value: Vec<u8>,
        state_root: core::StateRoot,
    },
}

#[allow(clippy::too_many_arguments)]
pub fn initialize(
    authenticated_owner: core::AccountId,
    palace_id: core::StableId,
    title: String,
    active_manifest_cid: String,
    owner_profile: core::UserProfileInput,
    owner_grant_id: core::GrantId,
    entry_room_id: core::RoomId,
    entry_room: core::RoomConfigInput,
    secondary_room_id: core::RoomId,
    secondary_room: core::RoomConfigInput,
) -> Result<core::PalaceGenesis, core::PalaceError> {
    core::PalaceGenesis::create(
        authenticated_owner,
        palace_id,
        title,
        active_manifest_cid,
        owner_profile,
        owner_grant_id,
        entry_room_id,
        entry_room,
        secondary_room_id,
        secondary_room,
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn room(title: &str, id: u8) -> core::RoomConfigInput {
        core::RoomConfigInput {
            title: title.into(),
            manifest_cid: format!("bafy{id}roommanifest"),
            script_bundle_cid: format!("bafy{id}scriptbundle"),
            vm_profile: core::VmProfile::IptScraeMvpV1,
        }
    }

    #[test]
    fn initialize_builds_the_discriminated_account_graph() {
        let genesis = initialize(
            [1; 32],
            [2; 32],
            "Palace".into(),
            "bafypalacemanifest".into(),
            core::UserProfileInput {
                display_name: "Alice".into(),
                delivery_key: [3; 32],
                key_epoch: 1,
                avatar_manifest_cid: Some("bafyavatar".into()),
            },
            [4; 32],
            [5; 32],
            room("Atrium", 1),
            [6; 32],
            room("Lounge", 2),
        )
        .expect("valid graph");

        assert_eq!(genesis.root.record_type, core::RecordType::PalaceRoot);
        assert_eq!(
            genesis.owner_profile.record_type,
            core::RecordType::UserProfile
        );
        assert_eq!(genesis.rooms[0].record_type, core::RecordType::Room);
        assert_eq!(
            genesis.owner_grant.record_type,
            core::RecordType::CapabilityGrant
        );
        assert_eq!(genesis.root.room_ids, [[5; 32], [6; 32]]);
    }

    #[test]
    fn every_instruction_variant_round_trips_without_schema_loss() {
        let fixtures = vec![
            GuestInstruction::Initialize {
                palace_id: [1; 32],
                title: "Palace".into(),
                active_manifest_cid: "bafypalace".into(),
                owner_profile: Box::new(core::UserProfileInput {
                    display_name: "Alice".into(),
                    delivery_key: [2; 32],
                    key_epoch: 1,
                    avatar_manifest_cid: None,
                }),
                owner_grant_id: [3; 32],
                entry_room_id: [4; 32],
                entry_room: Box::new(room("Atrium", 1)),
                secondary_room_id: [5; 32],
                secondary_room: Box::new(room("Lounge", 2)),
            },
            GuestInstruction::RegisterUser {
                ordered_action_id: 1,
                profile: core::UserProfileInput {
                    display_name: "Bob".into(),
                    delivery_key: [6; 32],
                    key_epoch: 1,
                    avatar_manifest_cid: Some("bafyavatar".into()),
                },
            },
            GuestInstruction::UpdateUserProfile {
                ordered_action_id: 2,
                display_name: "Bobby".into(),
                avatar_manifest_cid: None,
            },
            GuestInstruction::RotateDeliveryKey {
                ordered_action_id: 3,
                delivery_key: [7; 32],
                key_epoch: 2,
            },
            GuestInstruction::PublishManifest {
                ordered_action_id: 4,
                cid: "bafymanifest".into(),
            },
            GuestInstruction::UpdateRoom {
                ordered_action_id: 5,
                grant_id: [8; 32],
                room_id: [4; 32],
                config: room("Atrium v2", 3),
            },
            GuestInstruction::GrantCapability {
                ordered_action_id: 6,
                grant_id: [8; 32],
                subject_user_id: [9; 32],
                scope: core::PalaceScope::Palace,
                capabilities: core::CAP_MODERATE_USER,
                delegable: false,
                valid_through_action_id: 20,
            },
            GuestInstruction::RevokeCapability {
                ordered_action_id: 7,
                grant_id: [8; 32],
            },
            GuestInstruction::SetRoomLocked {
                ordered_action_id: 8,
                grant_id: [8; 32],
                room_id: [4; 32],
                locked: true,
            },
            GuestInstruction::CreateUserBan {
                ordered_action_id: 9,
                grant_id: [8; 32],
                ban_id: [10; 32],
                subject_user_id: [11; 32],
                scope: core::PalaceScope::Room([4; 32]),
            },
            GuestInstruction::CreateAssetBan {
                ordered_action_id: 10,
                grant_id: [8; 32],
                ban_id: [12; 32],
                cid: "bafyasset".into(),
                scope: core::PalaceScope::Palace,
            },
            GuestInstruction::SetBanActive {
                ordered_action_id: 11,
                grant_id: [8; 32],
                ban_id: [12; 32],
                active: false,
            },
            GuestInstruction::CreateSharedState {
                ordered_action_id: 12,
                grant_id: [8; 32],
                shared_state_id: [13; 32],
                room_id: [4; 32],
                key: "door".into(),
                value: vec![1],
                state_root: [14; 32],
            },
            GuestInstruction::UpdateSharedState {
                ordered_action_id: 13,
                grant_id: [8; 32],
                shared_state_id: [13; 32],
                room_id: [4; 32],
                state_revision: 2,
                value: vec![2],
                state_root: [15; 32],
            },
        ];

        for fixture in fixtures {
            let bytes = borsh::to_vec(&fixture).expect("instruction serializes");
            let decoded =
                GuestInstruction::try_from_slice(&bytes).expect("instruction deserializes");
            assert_eq!(decoded, fixture);
        }
    }
}
