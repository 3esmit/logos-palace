#![cfg_attr(not(test), no_main)]
#![allow(
    clippy::cloned_ref_to_slice_refs,
    reason = "SPEL macro emits cloned validation slices for one-account instructions"
)]

use borsh::BorshDeserialize;
use nssa_core::account::{Account, AccountWithMetadata, Data};
use spel_framework::context::ProgramContext;
use spel_framework::prelude::*;

#[cfg(not(test))]
risc0_zkvm::guest::entry!(main);

#[cfg(test)]
const PALACE_STATE_SEED: &str = "palace-state";

#[lez_program(instruction = "palace_program::GuestInstruction")]
mod palace {
    #[expect(
        unused_imports,
        reason = "SPEL instruction macro requires importing parent-scope handler types"
    )]
    use super::*;

    /// Create the single public Palace state account at the canonical PDA.
    #[instruction]
    pub fn initialize(
        _ctx: ProgramContext,
        #[account(init, pda = literal("palace-state"))] state: AccountWithMetadata,
        #[account(signer)] owner: AccountWithMetadata,
        owner_delivery_key: palace_program::core::DeliveryKey,
        rooms: Vec<palace_program::core::Room>,
    ) -> SpelResult {
        let palace_state =
            palace_program::initialize(owner.account_id.into_value(), owner_delivery_key, rooms)
                .map_err(palace_error)?;
        let state = write_state(state, &palace_state)?;

        Ok(SpelOutput::execute(vec![state, owner], vec![]))
    }

    /// Apply one transition to the canonical state after LEZ has authenticated
    /// the caller and verified that this program owns the account.
    #[instruction]
    pub fn apply(
        _ctx: ProgramContext,
        #[account(mut, owner = self_program_id, pda = literal("palace-state"))]
        state: AccountWithMetadata,
        #[account(signer)] caller: AccountWithMetadata,
        instruction: palace_program::core::PalaceInstruction,
    ) -> SpelResult {
        let mut palace_state = read_state(&state)?;
        palace_program::execute(
            &mut palace_state,
            caller.account_id.into_value(),
            instruction,
        )
        .map_err(palace_error)?;
        let state = write_state(state, &palace_state)?;

        Ok(SpelOutput::execute(vec![state, caller], vec![]))
    }

    fn read_state(
        state: &AccountWithMetadata,
    ) -> Result<palace_program::core::PalaceState, SpelError> {
        palace_program::core::PalaceState::try_from_slice(state.account.data.as_ref()).map_err(
            |error| SpelError::DeserializationError {
                account_index: 0,
                message: error.to_string(),
            },
        )
    }

    fn write_state(
        state: AccountWithMetadata,
        palace_state: &palace_program::core::PalaceState,
    ) -> Result<AccountWithMetadata, SpelError> {
        let bytes = borsh::to_vec(palace_state).map_err(|error| SpelError::SerializationError {
            message: error.to_string(),
        })?;
        let data = Data::try_from(bytes).map_err(|error| SpelError::SerializationError {
            message: error.to_string(),
        })?;
        Ok(AccountWithMetadata {
            account: Account {
                data,
                ..state.account
            },
            ..state
        })
    }

    fn palace_error(error: palace_program::core::PalaceError) -> SpelError {
        SpelError::custom(error.code(), "Palace state transition rejected")
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nssa_core::program::{Claim, ProgramId, DEFAULT_PROGRAM_ID};

    const PROGRAM_ID: ProgramId = [42; 8];
    const OWNER_ID: [u8; 32] = [1; 32];
    const CALLER_ID: [u8; 32] = [2; 32];

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

    fn rooms() -> Vec<palace_program::core::Room> {
        vec![
            palace_program::core::Room {
                id: "atrium".into(),
                title: "Atrium".into(),
            },
            palace_program::core::Room {
                id: "lounge".into(),
                title: "Lounge".into(),
            },
        ]
    }

    #[test]
    fn initialize_claims_the_canonical_pda_and_stores_owner() {
        let state = account([3; 32], false);
        let owner = account(OWNER_ID, true);
        let instruction_words = Vec::new();

        assert!(palace::__validate_initialize(
            &[state.clone(), owner.clone()],
            &PROGRAM_ID,
            &instruction_words,
        )
        .is_err());

        let canonical = spel_framework::pda::compute_pda(
            &PROGRAM_ID,
            &[&spel_framework::pda::seed_from_str(PALACE_STATE_SEED)],
        );
        let state = AccountWithMetadata {
            account_id: canonical,
            ..state
        };
        assert!(palace::__validate_initialize(
            &[state.clone(), owner.clone()],
            &PROGRAM_ID,
            &instruction_words,
        )
        .is_ok());

        let output = palace::initialize(context(), state, owner, [7; 32], rooms())
            .expect("valid Palace initialization");
        assert!(matches!(
            output.post_states[0].required_claim(),
            Some(Claim::Pda(_))
        ));
        let stored = palace_program::core::PalaceState::try_from_slice(
            output.post_states[0].account().data.as_ref(),
        )
        .expect("stored Palace state");
        assert_eq!(stored.owner, OWNER_ID);
        assert_eq!(stored.rooms, rooms());
    }

    #[test]
    fn apply_requires_owned_canonical_state_and_authenticated_owner() {
        let canonical = spel_framework::pda::compute_pda(
            &PROGRAM_ID,
            &[&spel_framework::pda::seed_from_str(PALACE_STATE_SEED)],
        );
        let initialized = palace::initialize(
            context(),
            AccountWithMetadata {
                account_id: canonical,
                ..account([3; 32], false)
            },
            account(OWNER_ID, true),
            [7; 32],
            rooms(),
        )
        .expect("valid initial state");
        let mut state = AccountWithMetadata {
            account: initialized.post_states[0].clone().into_account(),
            is_authorized: false,
            account_id: canonical,
        };
        state.account.program_owner = PROGRAM_ID;
        let owner = account(OWNER_ID, true);
        let instruction_words = Vec::new();

        assert!(palace::__validate_apply(
            &[state.clone(), owner.clone()],
            &PROGRAM_ID,
            &instruction_words,
        )
        .is_ok());
        let output = palace::apply(
            context(),
            state,
            owner,
            palace_program::core::PalaceInstruction::PublishManifest {
                cid: "bafybeiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".into(),
            },
        )
        .expect("owner transition succeeds");
        let stored = palace_program::core::PalaceState::try_from_slice(
            output.post_states[0].account().data.as_ref(),
        )
        .expect("updated state");
        assert_eq!(stored.revision, 1);
        assert_eq!(stored.manifests.len(), 1);

        let error = palace::apply(
            context(),
            AccountWithMetadata {
                account: output.post_states[0].account().clone(),
                is_authorized: false,
                account_id: canonical,
            },
            account(CALLER_ID, true),
            palace_program::core::PalaceInstruction::PublishManifest {
                cid: "bafybeiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".into(),
            },
        )
        .expect_err("non-owner must not mutate Palace state");
        assert_eq!(error.error_code(), 6_006);
    }
}
