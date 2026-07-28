//! Deterministic Palace program entrypoint.
//!
//! The LEZ guest adapter supplies the authenticated caller and serializes this
//! state through LEZ accounts. Keeping the transition here makes program rules
//! independently testable and prevents UI or Delivery events from bypassing
//! durable authority checks.

pub use palace_program_core as core;

pub fn execute(
    state: &mut core::PalaceState,
    authenticated_caller: core::AccountId,
    instruction: core::PalaceInstruction,
) -> Result<(), core::PalaceError> {
    state.apply(authenticated_caller, instruction)
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
                core::PalaceInstruction::PublishManifest {
                    cid: "bafybeiaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa".into(),
                },
            ),
            Ok(())
        );
    }
}
