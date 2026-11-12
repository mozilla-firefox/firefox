/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

//! Implements traversal over the DOM tree. The traversal starts in sequential
//! mode, and optionally parallelizes as it discovers work.

#![deny(missing_docs)]

use crate::context::{PerThreadTraversalStatistics, StyleContext};
use crate::context::{ThreadLocalStyleContext, TraversalStatistics};
use crate::dom::{SendNode, TElement, TNode};
use crate::parallel;
use crate::scoped_tls::ScopedTLS;
use crate::traversal::{DomTraversal, PreTraverseToken};
use std::collections::VecDeque;
use std::time::Instant;

#[cfg(feature = "servo")]
fn should_report_statistics() -> bool {
    false
}

#[cfg(feature = "gecko")]
fn should_report_statistics() -> bool {
    unsafe { !crate::gecko_bindings::structs::ServoTraversalStatistics_sSingleton.is_null() }
}

#[cfg(feature = "servo")]
fn report_statistics(_stats: &PerThreadTraversalStatistics) {
    unreachable!("Servo never report stats");
}

#[cfg(feature = "gecko")]
fn report_statistics(stats: &PerThreadTraversalStatistics) {
    // This should only be called in the main thread, or it may be racy
    // to update the statistics in a global variable.
    unsafe {
        debug_assert!(crate::gecko_bindings::bindings::Gecko_IsMainThread());
        let gecko_stats = crate::gecko_bindings::structs::ServoTraversalStatistics_sSingleton;
        (*gecko_stats).mElementsTraversed += stats.elements_traversed;
        (*gecko_stats).mElementsStyled += stats.elements_styled;
        (*gecko_stats).mElementsMatched += stats.elements_matched;
        (*gecko_stats).mStylesShared += stats.styles_shared;
        (*gecko_stats).mStylesReused += stats.styles_reused;
    }
}

fn with_pool_in_place_scope<'scope>(
    work_unit_max: usize,
    pool: Option<&rayon::ThreadPool>,
    closure: impl FnOnce(Option<&rayon::ScopeFifo<'scope>>) + Send + 'scope,
) {
    if work_unit_max == 0 || pool.is_none() {
        closure(None);
    } else {
        let pool = pool.unwrap();
        pool.in_place_scope_fifo(|scope| {
            #[cfg(feature = "gecko")]
            debug_assert_eq!(
                pool.current_thread_index(),
                Some(0),
                "Main thread should be the first thread"
            );
            if cfg!(feature = "gecko") || pool.current_thread_index().is_some() {
                closure(Some(scope));
            } else {
                scope.spawn_fifo(|scope| closure(Some(scope)));
            }
        });
    }
}

/// See documentation of the pref for performance characteristics.
fn work_unit_max() -> usize {
    crate::pref!("layout.css.stylo-work-unit-size") as usize
}

/// Do a DOM traversal for top-down and (optionally) bottom-up processing, generic over `D`.
///
/// We use an adaptive traversal strategy. We start out with simple sequential processing, until we
/// arrive at a wide enough level in the DOM that the parallel traversal would parallelize it.
/// If a thread pool is provided, we then transfer control over to the parallel traversal.
///
/// Returns true if the traversal was parallel, and also returns the statistics object containing
/// information on nodes traversed (on nightly only). Not all of its fields will be initialized
/// since we don't call finish().
pub fn traverse_dom<E, D>(
    traversal: &D,
    token: PreTraverseToken<E>,
    pool: Option<&rayon::ThreadPool>,
) -> E
where
    E: TElement,
    D: DomTraversal<E>,
{
    let root = token
        .traversal_root()
        .expect("Should've ensured we needed to traverse");

    let report_stats = should_report_statistics();
    let dump_stats = traversal.shared_context().options.dump_style_statistics;
    let start_time = if dump_stats {
        Some(Instant::now())
    } else {
        None
    };

    // Dropped before the sequential tasks run, see below.
    let mut scoped_tls = ScopedTLS::<ThreadLocalStyleContext<E>>::new(pool);
    // Process the nodes breadth-first. This helps keep similar traversal characteristics for the
    // style sharing cache.
    let work_unit_max = work_unit_max();

    let send_root = unsafe { SendNode::new(root.as_node()) };
    with_pool_in_place_scope(work_unit_max, pool, |maybe_scope| {
        let mut tlc = scoped_tls.ensure(parallel::create_thread_local_context);
        tlc.current_dom_depth = send_root.depth();

        let mut context = StyleContext {
            shared: traversal.shared_context(),
            thread_local: &mut tlc,
        };

        let mut discovered = VecDeque::with_capacity(work_unit_max * 2);
        let opaque_root = send_root.opaque();
        discovered.push_back(send_root);
        parallel::style_trees(
            &mut context,
            discovered,
            opaque_root,
            work_unit_max,
            maybe_scope,
            traversal,
            &scoped_tls,
        );
    });

    // Collect statistics from thread-locals if requested.
    if dump_stats || report_stats {
        let mut aggregate = PerThreadTraversalStatistics::default();
        for slot in scoped_tls.slots() {
            if let Some(cx) = slot.get_mut() {
                aggregate += cx.statistics.clone();
            }
        }

        if report_stats {
            report_statistics(&aggregate);
        }
        // dump statistics to stdout if requested
        if dump_stats {
            let parallel = pool.is_some();
            let stats =
                TraversalStatistics::new(aggregate, traversal, parallel, start_time.unwrap());
            if stats.is_large {
                println!("{}", stats);
            }
        }
    }

    // Run the sequential tasks the traversal queued. The thread-local contexts
    // must be gone first, since a task may call into functions like
    // Servo_StyleSet_GetBaseComputedValuesForElement, which create a
    // ThreadLocalStyleContext on the main thread and would otherwise
    // double-borrow the sharing cache and bloom filter storage in TLS.
    let mut tasks = Vec::new();
    for slot in scoped_tls.slots() {
        if let Some(cx) = slot.get_mut() {
            tasks.append(&mut cx.tasks);
        }
    }
    drop(scoped_tls);
    if !tasks.is_empty() {
        for task in tasks {
            task.execute();
        }
        traversal.after_sequential_tasks(root);
    }

    root
}
