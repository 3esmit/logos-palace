//! Deterministic Palace program entrypoint.
//!
//! The LEZ guest adapter supplies the authenticated caller and serializes this
//! state through LEZ accounts. Keeping the transition here makes program rules
//! independently testable and prevents UI or Delivery events from bypassing
//! durable authority checks.

use borsh::{BorshDeserialize, BorshSerialize};
use serde::{Deserialize, Serialize};

pub use palace_program_core as core;

/// LEZ-facing instruction envelope.
///
/// `Initialize` creates the one canonical public Palace state account. `Apply`
/// carries the next ordered action ID and an already-versioned Palace
/// transition, which the guest runs only after LEZ has authenticated the caller
/// and verified state-account ownership.
#[derive(Clone, Debug, Eq, PartialEq, BorshDeserialize, BorshSerialize, Deserialize, Serialize)]
pub enum GuestInstruction {
    Initialize {
        owner_delivery_key: core::DeliveryKey,
        rooms: Vec<core::Room>,
    },
    Apply {
        ordered_action_id: u64,
        instruction: core::PalaceInstruction,
    },
}

pub fn initialize(
    authenticated_owner: core::AccountId,
    owner_delivery_key: core::DeliveryKey,
    rooms: Vec<core::Room>,
) -> Result<core::PalaceState, core::PalaceError> {
    core::PalaceState::create(authenticated_owner, owner_delivery_key, rooms)
}

pub fn execute(
    state: &mut core::PalaceState,
    authenticated_caller: core::AccountId,
    ordered_action_id: u64,
    instruction: core::PalaceInstruction,
) -> Result<(), core::PalaceError> {
    state.apply(authenticated_caller, ordered_action_id, instruction)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn entrypoint_delegates_to_the_versioned_core_contract() {
        let owner = [1; 32];
        let mut state = core::PalaceState::create(
            owner,
            [7; 32],
            vec![
                core::Room {
                    id: "atrium".into(),
                    title: "Atrium".into(),
                },
                core::Room {
                    id: "lounge".into(),
                    title: "Lounge".into(),
                },
            ],
        )
        .expect("valid MVP palace");

        assert_eq!(
            execute(
                &mut state,
                owner,
                1,
                core::PalaceInstruction::PublishManifest {
                    cid: "bafybeiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".into(),
                },
            ),
            Ok(())
        );
    }

    #[test]
    fn lez_instruction_envelope_round_trips_without_schema_loss() {
        let instruction = GuestInstruction::Apply {
            ordered_action_id: 9,
            instruction: core::PalaceInstruction::SetRoomLocked {
                room_id: "lounge".into(),
                locked: true,
            },
        };

        let bytes = borsh::to_vec(&instruction).expect("instruction must serialize");
        assert_eq!(
            GuestInstruction::try_from_slice(&bytes).expect("instruction must deserialize"),
            instruction
        );
    }
}
