/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Implements parallel traversal over the DOM tree.
//!
//! This traversal is based on Rayon, and therefore its safety is largely
//! verified by the type system.
//!
//! The primary trickiness and fine print for the above relates to the
//! thread safety of the DOM nodes themselves. Accessing a DOM element
//! concurrently on multiple threads is actually mostly "safe", since all
//! the mutable state is protected by an AtomicRefCell, and so we'll
//! generally panic if something goes wrong. Still, we try to to enforce our
//! thread invariants at compile time whenever possible. As such, TNode and
//! TElement are not Send, so ordinary style system code cannot accidentally
//! share them with other threads. In the parallel traversal, we explicitly
//! invoke |unsafe { SendNode::new(n) }| to put nodes in containers that may
//! be sent to other threads. This occurs in only a handful of places and is
//! easy to grep for. At the time of this writing, there is no other unsafe
//! code in the parallel traversal.

#![deny(missing_docs)]

use crate::context::{StyleContext, ThreadLocalStyleContext};
use crate::dom::{OpaqueNode, SendNode, TElement};
use crate::scoped_tls::ScopedTLS;
use crate::traversal::DomTraversal;
use std::collections::VecDeque;

/// The minimum stack size for a thread in the styling pool, in kilobytes.
#[cfg(feature = "gecko")]
pub const STYLE_THREAD_STACK_SIZE_KB: usize = 256;

/// The minimum stack size for a thread in the styling pool, in kilobytes.
/// Servo requires a bigger stack in debug builds.
/// We allow configuring the size, since running with ASAN requires an even larger
/// stack size.
#[cfg(feature = "servo")]
pub const STYLE_THREAD_STACK_SIZE_KB: usize = const {
    let default_stack_size = 512;
    if let Some(user_def_size) = option_env!("SERVO_STYLE_THREAD_STACK_SIZE_KB") {
        if let Ok(user_def_size) = usize::from_str_radix(user_def_size, 10) {
            user_def_size
        } else {
            panic!("SERVO_STYLE_THREAD_STACK_SIZE_KB must be a valid integer")
        }
    } else {
        default_stack_size
    }
};

/// The stack margin. If we get this deep in the stack, we will skip recursive
/// optimizations to ensure that there is sufficient room for non-recursive work.
///
/// We allocate large safety margins because certain OS calls can use very large
/// amounts of stack space [1]. Reserving a larger-than-necessary stack costs us
/// address space, but if we keep our safety margin big, we will generally avoid
/// committing those extra pages, and only use them in edge cases that would
/// otherwise cause crashes.
///
/// When measured with 128KB stacks and 40KB margin, we could support 53
/// levels of recursion before the limiter kicks in, on x86_64-Linux [2]. When
/// we doubled the stack size, we added it all to the safety margin, so we should
/// be able to get the same amount of recursion.
///
/// [1] https://bugzilla.mozilla.org/show_bug.cgi?id=1395708#c15
/// [2] See Gecko bug 1376883 for more discussion on the measurements.
pub const STACK_SAFETY_MARGIN_KB: usize = 168;

/// A callback to create our thread local context.  This needs to be
/// out of line so we don't allocate stack space for the entire struct
/// in the caller.
#[inline(never)]
pub(crate) fn create_thread_local_context<'scope, E>(slot: &mut Option<ThreadLocalStyleContext<E>>)
where
    E: TElement + 'scope,
{
    *slot = Some(ThreadLocalStyleContext::new());
}

// Sends one chunk of work to the thread-pool.
fn distribute_one_chunk<'a, 'scope, E, D>(
    items: VecDeque<SendNode<E::ConcreteNode>>,
    traversal_root: OpaqueNode,
    work_unit_max: usize,
    dom_depth: usize,
    scope: &'a rayon::ScopeFifo<'scope>,
    traversal: &'scope D,
    tls: &'scope ScopedTLS<'scope, ThreadLocalStyleContext<E>>,
) where
    E: TElement + 'scope,
    D: DomTraversal<E>,
{
    scope.spawn_fifo(move |scope| {
        #[cfg(feature = "gecko")]
        gecko_profiler_label!(Layout, StyleComputation);
        let mut tlc = tls.ensure(create_thread_local_context);
        tlc.current_dom_depth = dom_depth;
        let mut context = StyleContext {
            shared: traversal.shared_context(),
            thread_local: &mut *tlc,
        };
        style_trees(
            &mut context,
            items,
            traversal_root,
            work_unit_max,
            Some(scope),
            traversal,
            tls,
        );
    })
}

/// Distributes all items into the thread pool, in `work_unit_max` chunks.
fn distribute_work<'a, 'scope, E, D>(
    mut items: impl Iterator<Item = SendNode<E::ConcreteNode>>,
    traversal_root: OpaqueNode,
    work_unit_max: usize,
    dom_depth: usize,
    scope: &'a rayon::ScopeFifo<'scope>,
    traversal: &'scope D,
    tls: &'scope ScopedTLS<'scope, ThreadLocalStyleContext<E>>,
) where
    E: TElement + 'scope,
    D: DomTraversal<E>,
{
    use std::iter::FromIterator;
    loop {
        let chunk = VecDeque::from_iter(items.by_ref().take(work_unit_max));
        if chunk.is_empty() {
            return;
        }
        distribute_one_chunk(
            chunk,
            traversal_root,
            work_unit_max,
            dom_depth,
            scope,
            traversal,
            tls,
        );
    }
}

/// Processes `discovered` items, possibly spawning work in other threads as needed.
#[inline]
pub fn style_trees<'a, 'scope, E, D>(
    context: &mut StyleContext<E>,
    mut discovered: VecDeque<SendNode<E::ConcreteNode>>,
    traversal_root: OpaqueNode,
    work_unit_max: usize,
    scope: Option<&'a rayon::ScopeFifo<'scope>>,
    traversal: &'scope D,
    tls: &'scope ScopedTLS<'scope, ThreadLocalStyleContext<E>>,
) where
    E: TElement + 'scope,
    D: DomTraversal<E>,
{
    let local_queue_size = if tls.current_thread_index() == 0 {
        crate::pref!("layout.css.stylo-local-work-queue.in-main-thread")
    } else {
        crate::pref!("layout.css.stylo-local-work-queue.in-worker")
    } as usize;

    let mut nodes_remaining_at_current_depth = discovered.len();
    while let Some(node) = discovered.pop_front() {
        let mut children_to_process = 0isize;
        traversal.process_preorder(context, *node, |n| {
            children_to_process += 1;
            discovered.push_back(unsafe { SendNode::new(n) });
        });

        traversal.handle_postorder_traversal(context, traversal_root, *node, children_to_process);

        nodes_remaining_at_current_depth -= 1;

        // If we have enough children at the next depth in the DOM, spawn them to a different job
        // relatively soon, while keeping always at least `local_queue_size` worth of work for
        // ourselves.
        let keep_work = std::cmp::max(nodes_remaining_at_current_depth, local_queue_size);
        if discovered.len() >= keep_work + work_unit_max && scope.is_some() {
            distribute_work(
                discovered.range(keep_work..).cloned(),
                traversal_root,
                work_unit_max,
                context.thread_local.current_dom_depth + 1,
                scope.unwrap(),
                traversal,
                tls,
            );
            discovered.truncate(keep_work);
        }

        if nodes_remaining_at_current_depth == 0 {
            context.thread_local.current_dom_depth += 1;
            nodes_remaining_at_current_depth = discovered.len();
        }
    }
}
