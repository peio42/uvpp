# uvpp Documentation

uvpp is a header-only C++20 wrapper around libuv. Include `uvpp/uv.hpp`, use
namespace `uv`, and link with libuv and pthread.

- [User documentation](user/index.md): the supported API, guides, and
  [tutorial](user/tutorial/index.md).
- [Current design](design/index.md): implemented architecture, contracts, and
  contributor rules.
- [Proposals](proposals/index.md): future changes, alternatives, open decisions,
  and implementation progress. Proposed APIs are not available unless explicitly
  recorded as implemented.

When a proposal is implemented, update the current design and user guides in the
same change. Keep the proposal as a decision record with links to those references.
