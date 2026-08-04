#![cfg_attr(not(test), no_main)]
#![allow(
    clippy::cloned_ref_to_slice_refs,
    reason = "SPEL macro emits cloned validation slices for one-account instructions"
)]

use borsh::{BorshDeserialize, BorshSerialize};
use nssa_core::account::{Account, AccountWithMetadata, Data};
use spel_framework::context::ProgramContext;
use spel_framework::prelude::*;

#[cfg(not(test))]
risc0_zkvm::guest::entry!(main);

#[cfg(test)]
const PALACE_ROOT_SEED: &str = "palace-root";
#[cfg(test)]
const PROFILE_SEED: &str = "profile";
#[cfg(test)]
const ROOM_SEED: &str = "room";
#[cfg(test)]
const GRANT_SEED: &str = "grant";

#[lez_program(instruction = "palace_program::GuestInstruction")]
mod palace {
    #[expect(
        unused_imports,
        reason = "SPEL instruction macro requires importing parent-scope handler types"
    )]
    use super::*;

    /// Create the canonical root and complete bounded MVP account graph.
    #[expect(
        clippy::boxed_local,
        reason = "boxed genesis fields keep the instruction enum stack size bounded"
    )]
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn initialize(
        _ctx: ProgramContext,
        #[account(init, pda = literal("palace-root"))] root: AccountWithMetadata,
        #[account(signer)] owner: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("profile"), account("root"), account("owner")]
        )]
        owner_profile_account: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("room"), account("root"), arg("entry_room_id")]
        )]
        entry_room_account: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("room"), account("root"), arg("secondary_room_id")]
        )]
        secondary_room_account: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("grant"), account("root"), arg("owner_grant_id")]
        )]
        owner_grant_account: AccountWithMetadata,
        palace_id: palace_program::core::StableId,
        title: String,
        active_manifest_cid: String,
        owner_profile: Box<palace_program::core::UserProfileInput>,
        owner_grant_id: palace_program::core::GrantId,
        entry_room_id: palace_program::core::RoomId,
        entry_room: Box<palace_program::core::RoomConfigInput>,
        secondary_room_id: palace_program::core::RoomId,
        secondary_room: Box<palace_program::core::RoomConfigInput>,
    ) -> SpelResult {
        let genesis = palace_program::initialize(
            account_id(&owner),
            palace_id,
            title,
            active_manifest_cid,
            *owner_profile,
            owner_grant_id,
            entry_room_id,
            *entry_room,
            secondary_room_id,
            *secondary_room,
        )
        .map_err(palace_error)?;
        let root = write_record(root, &genesis.root)?;
        let owner_profile_account = write_record(owner_profile_account, &genesis.owner_profile)?;
        let entry_room_account = write_record(entry_room_account, &genesis.rooms[0])?;
        let secondary_room_account = write_record(secondary_room_account, &genesis.rooms[1])?;
        let owner_grant_account = write_record(owner_grant_account, &genesis.owner_grant)?;

        Ok(SpelOutput::execute(
            vec![
                root,
                owner,
                owner_profile_account,
                entry_room_account,
                secondary_room_account,
                owner_grant_account,
            ],
            vec![],
        ))
    }

    /// Register the authenticated caller under the canonical profile PDA.
    #[instruction]
    pub fn register_user(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] user: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("profile"), account("root"), account("user")]
        )]
        profile_account: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("grant"), account("root"), account("user")]
        )]
        ingress_grant_account: AccountWithMetadata,
        ordered_action_id: u64,
        profile: palace_program::core::UserProfileInput,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let (profile, ingress_grant) = palace_root
            .register_user_with_default_grant(
                account_id(&user), ordered_action_id, profile)
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let profile_account = write_record(profile_account, &profile)?;
        let ingress_grant_account =
            write_record(ingress_grant_account, &ingress_grant)?;

        Ok(SpelOutput::execute(
            vec![root, user, profile_account, ingress_grant_account],
            vec![],
        ))
    }

    /// Update display and avatar fields on the authenticated caller's profile.
    #[instruction]
    pub fn update_user_profile(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] user: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("profile"), account("root"), account("user")]
        )]
        profile: AccountWithMetadata,
        ordered_action_id: u64,
        display_name: String,
        avatar_manifest_cid: Option<String>,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let mut user_profile = read_record::<palace_program::core::UserProfile>(&profile, 2)?;
        palace_root
            .update_user_profile(
                account_id(&user),
                &mut user_profile,
                ordered_action_id,
                display_name,
                avatar_manifest_cid,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let profile = write_record(profile, &user_profile)?;
        Ok(SpelOutput::execute(vec![root, user, profile], vec![]))
    }

    /// Rotate the authenticated caller's Delivery key to a higher epoch.
    #[instruction]
    pub fn rotate_delivery_key(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] user: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("profile"), account("root"), account("user")]
        )]
        profile: AccountWithMetadata,
        ordered_action_id: u64,
        delivery_key: palace_program::core::DeliveryKey,
        key_epoch: u64,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let mut user_profile = read_record::<palace_program::core::UserProfile>(&profile, 2)?;
        palace_root
            .rotate_delivery_key(
                account_id(&user),
                &mut user_profile,
                ordered_action_id,
                delivery_key,
                key_epoch,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let profile = write_record(profile, &user_profile)?;
        Ok(SpelOutput::execute(vec![root, user, profile], vec![]))
    }

    /// Replace the active Palace manifest. Owner only.
    #[instruction]
    pub fn publish_manifest(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] owner: AccountWithMetadata,
        ordered_action_id: u64,
        cid: String,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        palace_root
            .publish_manifest(account_id(&owner), ordered_action_id, cid)
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        Ok(SpelOutput::execute(vec![root, owner], vec![]))
    }

    /// Replace one room's manifest, script, and VM profile through room-edit authority.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn update_room(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("room"), account("root"), arg("room_id")]
        )]
        room: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        room_id: palace_program::core::RoomId,
        config: palace_program::core::RoomConfigInput,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let mut room_record = read_record::<palace_program::core::RoomRecord>(&room, 3)?;
        palace_root
            .update_room(
                account_id(&caller),
                &grant_record,
                &mut room_record,
                ordered_action_id,
                grant_id,
                room_id,
                config,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let room = write_record(room, &room_record)?;
        Ok(SpelOutput::execute(vec![root, caller, grant, room], vec![]))
    }

    /// Create a bounded capability grant. Palace owner only.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn grant_capability(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] owner: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("profile"), account("root"), arg("subject_user_id")]
        )]
        subject_profile: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        subject_user_id: palace_program::core::AccountId,
        scope: palace_program::core::PalaceScope,
        capabilities: u32,
        delegable: bool,
        valid_through_action_id: u64,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let subject = read_record::<palace_program::core::UserProfile>(&subject_profile, 2)?;
        if subject.user_id != subject_user_id {
            return Err(palace_error(
                palace_program::core::PalaceError::RecordMismatch,
            ));
        }
        let grant_record = palace_root
            .grant_capability(
                account_id(&owner),
                &subject,
                ordered_action_id,
                grant_id,
                scope,
                capabilities,
                delegable,
                valid_through_action_id,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let grant = write_record(grant, &grant_record)?;
        Ok(SpelOutput::execute(
            vec![root, owner, subject_profile, grant],
            vec![],
        ))
    }

    /// Revoke a capability grant. Palace owner only.
    #[instruction]
    pub fn revoke_capability(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] owner: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let mut grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        palace_root
            .revoke_capability(
                account_id(&owner),
                &mut grant_record,
                ordered_action_id,
                grant_id,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let grant = write_record(grant, &grant_record)?;
        Ok(SpelOutput::execute(vec![root, owner, grant], vec![]))
    }

    /// Change room lock state through an explicit capability grant.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn set_room_locked(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("room"), account("root"), arg("room_id")]
        )]
        room: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        room_id: palace_program::core::RoomId,
        locked: bool,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let mut room_record = read_record::<palace_program::core::RoomRecord>(&room, 3)?;
        palace_root
            .set_room_locked(
                account_id(&caller),
                &grant_record,
                &mut room_record,
                ordered_action_id,
                grant_id,
                room_id,
                locked,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let room = write_record(room, &room_record)?;
        Ok(SpelOutput::execute(vec![root, caller, grant, room], vec![]))
    }

    /// Ban one registered user through an explicit moderation capability.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn create_user_ban(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("profile"), account("root"), arg("subject_user_id")]
        )]
        subject_profile: AccountWithMetadata,
        #[account(init, pda = [literal("ban"), account("root"), arg("ban_id")])]
        ban: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        ban_id: palace_program::core::BanId,
        subject_user_id: palace_program::core::AccountId,
        scope: palace_program::core::PalaceScope,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let subject = read_record::<palace_program::core::UserProfile>(&subject_profile, 3)?;
        if subject.user_id != subject_user_id {
            return Err(palace_error(
                palace_program::core::PalaceError::RecordMismatch,
            ));
        }
        let ban_record = palace_root
            .create_user_ban(
                account_id(&caller),
                &grant_record,
                &subject,
                ordered_action_id,
                grant_id,
                ban_id,
                scope,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let ban = write_record(ban, &ban_record)?;
        Ok(SpelOutput::execute(
            vec![root, caller, grant, subject_profile, ban],
            vec![],
        ))
    }

    /// Ban one Storage CID through an explicit moderation capability.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn create_asset_ban(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(init, pda = [literal("ban"), account("root"), arg("ban_id")])]
        ban: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        ban_id: palace_program::core::BanId,
        cid: String,
        scope: palace_program::core::PalaceScope,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let ban_record = palace_root
            .create_asset_ban(
                account_id(&caller),
                &grant_record,
                ordered_action_id,
                grant_id,
                ban_id,
                cid,
                scope,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let ban = write_record(ban, &ban_record)?;
        Ok(SpelOutput::execute(vec![root, caller, grant, ban], vec![]))
    }

    /// Activate or deactivate an existing ban through matching moderation.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn set_ban_active(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("ban"), account("root"), arg("ban_id")]
        )]
        ban: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        ban_id: palace_program::core::BanId,
        active: bool,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let mut ban_record = read_record::<palace_program::core::Ban>(&ban, 3)?;
        palace_root
            .set_ban_active(
                account_id(&caller),
                &grant_record,
                &mut ban_record,
                ordered_action_id,
                grant_id,
                ban_id,
                active,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let ban = write_record(ban, &ban_record)?;
        Ok(SpelOutput::execute(vec![root, caller, grant, ban], vec![]))
    }

    /// Create one bounded room shared-state record through explicit authority.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn create_shared_state(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("room"), account("root"), arg("room_id")]
        )]
        room: AccountWithMetadata,
        #[account(
            init,
            pda = [literal("shared"), account("root"), arg("shared_state_id")]
        )]
        shared: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        shared_state_id: palace_program::core::SharedStateId,
        room_id: palace_program::core::RoomId,
        key: String,
        value: Vec<u8>,
        state_root: palace_program::core::StateRoot,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let room_record = read_record::<palace_program::core::RoomRecord>(&room, 3)?;
        let shared_record = palace_root
            .create_shared_state(
                account_id(&caller),
                &grant_record,
                &room_record,
                ordered_action_id,
                grant_id,
                shared_state_id,
                room_id,
                key,
                value,
                state_root,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let shared = write_record(shared, &shared_record)?;
        Ok(SpelOutput::execute(
            vec![root, caller, grant, room, shared],
            vec![],
        ))
    }

    /// Advance one bounded room shared-state record through explicit authority.
    #[instruction]
    #[allow(clippy::too_many_arguments)]
    pub fn update_shared_state(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-root"))]
        root: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("grant"), account("root"), arg("grant_id")]
        )]
        grant: AccountWithMetadata,
        #[account(
            owner = self_program_id,
            pda = [literal("room"), account("root"), arg("room_id")]
        )]
        room: AccountWithMetadata,
        #[account(
            mut,
            owner = self_program_id,
            pda = [literal("shared"), account("root"), arg("shared_state_id")]
        )]
        shared: AccountWithMetadata,
        ordered_action_id: u64,
        grant_id: palace_program::core::GrantId,
        shared_state_id: palace_program::core::SharedStateId,
        room_id: palace_program::core::RoomId,
        state_revision: u64,
        value: Vec<u8>,
        state_root: palace_program::core::StateRoot,
    ) -> SpelResult {
        let mut palace_root = read_record::<palace_program::core::PalaceRoot>(&root, 0)?;
        let grant_record = read_record::<palace_program::core::CapabilityGrant>(&grant, 2)?;
        let room_record = read_record::<palace_program::core::RoomRecord>(&room, 3)?;
        let mut shared_record = read_record::<palace_program::core::RoomSharedState>(&shared, 4)?;
        palace_root
            .update_shared_state(
                account_id(&caller),
                &grant_record,
                &room_record,
                &mut shared_record,
                ordered_action_id,
                grant_id,
                shared_state_id,
                room_id,
                state_revision,
                value,
                state_root,
            )
            .map_err(palace_error)?;
        let root = write_record(root, &palace_root)?;
        let shared = write_record(shared, &shared_record)?;
        Ok(SpelOutput::execute(
            vec![root, caller, grant, room, shared],
            vec![],
        ))
    }

    fn account_id(account: &AccountWithMetadata) -> palace_program::core::AccountId {
        account.account_id.into_value()
    }

    trait ValidPalaceRecord {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError>;
    }

    impl ValidPalaceRecord for palace_program::core::PalaceRoot {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError> {
            self.validate()
        }
    }

    impl ValidPalaceRecord for palace_program::core::UserProfile {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError> {
            self.validate()
        }
    }

    impl ValidPalaceRecord for palace_program::core::RoomRecord {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError> {
            self.validate()
        }
    }

    impl ValidPalaceRecord for palace_program::core::CapabilityGrant {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError> {
            self.validate()
        }
    }

    impl ValidPalaceRecord for palace_program::core::Ban {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError> {
            self.validate()
        }
    }

    impl ValidPalaceRecord for palace_program::core::RoomSharedState {
        fn validate_record(&self) -> Result<(), palace_program::core::PalaceError> {
            self.validate()
        }
    }

    fn read_record<T>(account: &AccountWithMetadata, account_index: usize) -> Result<T, SpelError>
    where
        T: BorshDeserialize + ValidPalaceRecord,
    {
        let record = T::try_from_slice(account.account.data.as_ref()).map_err(|error| {
            SpelError::DeserializationError {
                account_index,
                message: error.to_string(),
            }
        })?;
        record.validate_record().map_err(palace_error)?;
        Ok(record)
    }

    fn write_record<T>(
        account: AccountWithMetadata,
        record: &T,
    ) -> Result<AccountWithMetadata, SpelError>
    where
        T: BorshSerialize + ValidPalaceRecord,
    {
        record.validate_record().map_err(palace_error)?;
        let bytes = borsh::to_vec(record).map_err(|error| SpelError::SerializationError {
            message: error.to_string(),
        })?;
        let data = Data::try_from(bytes).map_err(|error| SpelError::SerializationError {
            message: error.to_string(),
        })?;
        Ok(AccountWithMetadata {
            account: Account {
                data,
                ..account.account
            },
            ..account
        })
    }

    fn palace_error(error: palace_program::core::PalaceError) -> SpelError {
        SpelError::custom(error.code(), "Palace record transition rejected")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nssa_core::program::{Claim, ProgramId, DEFAULT_PROGRAM_ID};

    const PROGRAM_ID: ProgramId = [42; 8];
    const ALICE: [u8; 32] = [1; 32];
    const BOB: [u8; 32] = [2; 32];
    const PALACE_ID: [u8; 32] = [3; 32];
    const ATRIUM_ID: [u8; 32] = [4; 32];
    const LOUNGE_ID: [u8; 32] = [5; 32];
    const OWNER_GRANT_ID: [u8; 32] = [6; 32];

    fn account(id: [u8; 32], authorized: bool) -> AccountWithMetadata {
        AccountWithMetadata {
            account: Account::default(),
            is_authorized: authorized,
            account_id: nssa_core::account::AccountId::new(id),
        }
    }

    fn context() -> ProgramContext {
        ProgramContext::new(PROGRAM_ID, DEFAULT_PROGRAM_ID)
    }

    fn cid(label: &str) -> String {
        format!("bafy{label}aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
    }

    fn profile(name: &str, key: u8) -> palace_program::core::UserProfileInput {
        palace_program::core::UserProfileInput {
            display_name: name.into(),
            delivery_key: [key; 32],
            key_epoch: 1,
            avatar_manifest_cid: Some(cid("avatar")),
        }
    }

    fn room(title: &str, label: &str) -> palace_program::core::RoomConfigInput {
        palace_program::core::RoomConfigInput {
            title: title.into(),
            manifest_cid: cid(label),
            script_bundle_cid: cid("script"),
            vm_profile: palace_program::core::VmProfile::IptScraeMvpV1,
        }
    }

    fn pda(seeds: &[&[u8; 32]]) -> nssa_core::account::AccountId {
        spel_framework::pda::compute_pda(&PROGRAM_ID, seeds)
    }

    fn literal(value: &str) -> [u8; 32] {
        spel_framework::pda::seed_from_str(value)
    }

    #[test]
    fn initialize_validates_all_multi_seed_pdas_and_claims_each_record() {
        let root_id = pda(&[&literal(PALACE_ROOT_SEED)]);
        let owner_id = nssa_core::account::AccountId::new(ALICE);
        let owner_profile_id = pda(&[&literal(PROFILE_SEED), root_id.value(), owner_id.value()]);
        let entry_room_account_id = pda(&[&literal(ROOM_SEED), root_id.value(), &ATRIUM_ID]);
        let secondary_room_account_id = pda(&[&literal(ROOM_SEED), root_id.value(), &LOUNGE_ID]);
        let grant_account_id = pda(&[&literal(GRANT_SEED), root_id.value(), &OWNER_GRANT_ID]);
        let accounts = vec![
            AccountWithMetadata {
                account_id: root_id,
                ..account([20; 32], false)
            },
            account(ALICE, true),
            AccountWithMetadata {
                account_id: owner_profile_id,
                ..account([21; 32], false)
            },
            AccountWithMetadata {
                account_id: entry_room_account_id,
                ..account([22; 32], false)
            },
            AccountWithMetadata {
                account_id: secondary_room_account_id,
                ..account([23; 32], false)
            },
            AccountWithMetadata {
                account_id: grant_account_id,
                ..account([24; 32], false)
            },
        ];
        assert!(palace::__validate_initialize(
            &accounts,
            &PROGRAM_ID,
            &Vec::new(),
            &entry_room_id_fixture(),
            &secondary_room_id_fixture(),
            &owner_grant_id_fixture(),
        )
        .is_ok());

        let mut confused = accounts.clone();
        confused.swap(3, 4);
        assert!(palace::__validate_initialize(
            &confused,
            &PROGRAM_ID,
            &Vec::new(),
            &entry_room_id_fixture(),
            &secondary_room_id_fixture(),
            &owner_grant_id_fixture(),
        )
        .is_err());

        let output = palace::initialize(
            context(),
            accounts[0].clone(),
            accounts[1].clone(),
            accounts[2].clone(),
            accounts[3].clone(),
            accounts[4].clone(),
            accounts[5].clone(),
            PALACE_ID,
            "Public Palace".into(),
            cid("palace"),
            Box::new(profile("Alice", 7)),
            OWNER_GRANT_ID,
            ATRIUM_ID,
            Box::new(room("Atrium", "atrium")),
            LOUNGE_ID,
            Box::new(room("Lounge", "lounge")),
        )
        .expect("valid initialization");
        for record in [0, 2, 3, 4, 5] {
            assert!(matches!(
                output.post_states[record].required_claim(),
                Some(Claim::Pda(_))
            ));
        }
        let stored_root = palace_program::core::PalaceRoot::try_from_slice(
            output.post_states[0].account().data.as_ref(),
        )
        .expect("stored root");
        let stored_profile = palace_program::core::UserProfile::try_from_slice(
            output.post_states[2].account().data.as_ref(),
        )
        .expect("stored profile");
        let stored_entry = palace_program::core::RoomRecord::try_from_slice(
            output.post_states[3].account().data.as_ref(),
        )
        .expect("stored entry room");
        let stored_secondary = palace_program::core::RoomRecord::try_from_slice(
            output.post_states[4].account().data.as_ref(),
        )
        .expect("stored secondary room");
        let stored_grant = palace_program::core::CapabilityGrant::try_from_slice(
            output.post_states[5].account().data.as_ref(),
        )
        .expect("stored owner grant");
        assert_eq!(stored_root.owner, ALICE);
        assert_eq!(stored_root.room_ids, [ATRIUM_ID, LOUNGE_ID]);
        assert_eq!(stored_profile.user_id, ALICE);
        assert_eq!(stored_entry.room_id, ATRIUM_ID);
        assert_eq!(stored_secondary.room_id, LOUNGE_ID);
        assert_eq!(
            stored_grant.capabilities,
            palace_program::core::ALL_CAPABILITIES
        );
    }

    fn entry_room_id_fixture() -> [u8; 32] {
        ATRIUM_ID
    }

    fn secondary_room_id_fixture() -> [u8; 32] {
        LOUNGE_ID
    }

    fn owner_grant_id_fixture() -> [u8; 32] {
        OWNER_GRANT_ID
    }

    #[test]
    fn every_ordered_handler_keeps_root_and_signer_account_order_stable() {
        let idl = crate::__program_idl();
        assert_eq!(idl.instructions.len(), 14);
        for instruction in idl
            .instructions
            .iter()
            .filter(|instruction| instruction.name != "initialize")
        {
            assert_eq!(instruction.accounts[0].name, "root", "{}", instruction.name);
            assert!(instruction.accounts[0].writable, "{}", instruction.name);
            assert_eq!(
                instruction.accounts[0].owner.as_deref(),
                Some("self_program_id"),
                "{}",
                instruction.name
            );
            let root_pda = instruction.accounts[0]
                .pda
                .as_ref()
                .expect("root PDA is declared");
            assert_eq!(root_pda.seeds.len(), 1);
            assert!(matches!(
                &root_pda.seeds[0],
                spel_framework::idl::IdlSeed::Const { value } if value == PALACE_ROOT_SEED
            ));
            assert!(instruction.accounts[1].signer, "{}", instruction.name);
            assert_eq!(
                instruction.args[0].name, "ordered_action_id",
                "{}",
                instruction.name
            );
        }
    }

    #[test]
    fn guest_instruction_wire_fixture_is_exact() {
        let words = risc0_zkvm::serde::to_vec(&palace_program::GuestInstruction::SetRoomLocked {
            ordered_action_id: 0x0000_0002_0000_0001,
            grant_id: [0x11; 32],
            room_id: [0x22; 32],
            locked: true,
        })
        .expect("guest instruction serializes");
        let mut expected = vec![8, 1, 2];
        expected.extend([0x11; 32]);
        expected.extend([0x22; 32]);
        expected.push(1);
        assert_eq!(words, expected);

        let manifest_words =
            risc0_zkvm::serde::to_vec(&palace_program::GuestInstruction::PublishManifest {
                ordered_action_id: 0x0000_0002_0000_0001,
                cid: "bafy".into(),
            })
            .expect("manifest instruction serializes");
        assert_eq!(manifest_words, vec![4, 1, 2, 4, 0x7966_6162]);
    }

    #[test]
    fn owned_root_and_record_pdas_reject_confusion() {
        let root_id = pda(&[&literal(PALACE_ROOT_SEED)]);
        let grant_id = [30; 32];
        let room_id = ATRIUM_ID;
        let grant_account_id = pda(&[&literal(GRANT_SEED), root_id.value(), &grant_id]);
        let room_account_id = pda(&[&literal(ROOM_SEED), root_id.value(), &room_id]);
        let mut root = account(*root_id.value(), false);
        root.account.program_owner = PROGRAM_ID;
        let mut grant = account(*grant_account_id.value(), false);
        grant.account.program_owner = PROGRAM_ID;
        let mut room_account = account(*room_account_id.value(), false);
        room_account.account.program_owner = PROGRAM_ID;
        let caller = account(BOB, true);
        let accounts = [root.clone(), caller, grant.clone(), room_account.clone()];

        assert!(palace::__validate_set_room_locked(
            &accounts,
            &PROGRAM_ID,
            &Vec::new(),
            &grant_id,
            &room_id,
        )
        .is_ok());

        assert!(palace::__validate_set_room_locked(
            &[
                root.clone(),
                account(BOB, false),
                grant.clone(),
                room_account.clone(),
            ],
            &PROGRAM_ID,
            &Vec::new(),
            &grant_id,
            &room_id,
        )
        .is_err());

        grant.account.program_owner = DEFAULT_PROGRAM_ID;
        assert!(palace::__validate_set_room_locked(
            &[
                root.clone(),
                account(BOB, true),
                grant,
                room_account.clone()
            ],
            &PROGRAM_ID,
            &Vec::new(),
            &grant_id,
            &room_id,
        )
        .is_err());

        room_account.account_id = pda(&[&literal(ROOM_SEED), root_id.value(), &LOUNGE_ID]);
        assert!(palace::__validate_set_room_locked(
            &[root, account(BOB, true), accounts[2].clone(), room_account],
            &PROGRAM_ID,
            &Vec::new(),
            &grant_id,
            &room_id,
        )
        .is_err());
    }
}
