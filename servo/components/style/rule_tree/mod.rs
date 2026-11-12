/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#![deny(unsafe_code)]

//! The rule tree.

use crate::applicable_declarations::{
    ApplicableDeclarationBlock, ApplicableDeclarationList, CascadePriority,
};
use crate::properties::{LonghandIdSet, PropertyDeclarationBlock};
use crate::shared_lock::{Locked, StylesheetGuards};
use crate::stylesheets::layer_rule::LayerOrder;
use servo_arc::ArcBorrow;
use smallvec::SmallVec;
use std::io::{self, Write};

mod core;
pub mod level;
mod map;
mod source;
mod unsafe_box;

pub use self::core::{RuleTree, StrongRuleNode};
pub use self::level::{CascadeLevel, CascadeOrigin, ShadowCascadeOrder};
pub use self::source::{StyleSource, StyleSourceBorrow};

bitflags! {
    /// Flags that are part of the cascade priority, and that we use to track
    /// information about where the rule came from.
    #[derive(Clone, Copy, Debug, Default, Eq, Hash, PartialEq)]
    pub struct RuleCascadeFlags: u8 {
        /// Whether the rule is inside a @starting-style block.
        const STARTING_STYLE = 1 << 0;
        /// Whether the rule is inside an @appearance-base block.
        const APPEARANCE_BASE = 1 << 1;
    }
}

malloc_size_of::malloc_size_of_is_0!(RuleCascadeFlags);

/// The result of rule replacement.
pub struct RuleReplacementResult {
    /// The new rule node if any node was replaced.
    pub new_path: Option<StrongRuleNode>,
    /// Whether any important rules may have changed.
    pub important_changed: bool,
}

impl RuleTree {
    fn dump<W: Write>(&self, guards: &StylesheetGuards, writer: &mut W) {
        let _ = writeln!(writer, " + RuleTree");
        self.root().dump(guards, writer, 0);
    }

    /// Dump the rule tree to stdout.
    pub fn dump_stdout(&self, guards: &StylesheetGuards) {
        let mut stdout = io::stdout();
        self.dump(guards, &mut stdout);
    }

    /// Inserts the given rules, that must be in proper order by specifity, and
    /// returns the corresponding rule node representing the last inserted one.
    ///
    /// !important rules are detected and inserted into the appropriate position
    /// in the rule tree. This allows selector matching to ignore importance,
    /// while still maintaining the appropriate cascade order in the rule tree.
    pub fn insert_ordered_rules_with_important<'a, I>(
        &self,
        iter: I,
        guards: &StylesheetGuards,
    ) -> StrongRuleNode
    where
        I: Iterator<Item = (StyleSourceBorrow<'a>, CascadePriority)>,
    {
        let mut current = self.root().clone();

        let mut found_important = false;

        let mut important_author = SmallVec::<[(StyleSourceBorrow, CascadePriority); 4]>::new();
        let mut important_user = SmallVec::<[(StyleSourceBorrow, CascadePriority); 4]>::new();
        let mut important_ua = SmallVec::<[(StyleSourceBorrow, CascadePriority); 4]>::new();
        let mut transition = None;

        for (source, priority) in iter {
            let level = priority.cascade_level();
            debug_assert!(!level.is_important(), "Important levels handled internally");

            let any_important = {
                let pdb = source.read(level.guard(guards));
                pdb.any_important()
            };

            if any_important {
                found_important = true;
                match level.origin() {
                    CascadeOrigin::Author => important_author.push((source, priority.important())),
                    CascadeOrigin::UA => important_ua.push((source, priority.important())),
                    CascadeOrigin::User => important_user.push((source, priority.important())),
                    _ => {},
                };
            }

            // We don't optimize out empty rules, even though we could.
            //
            // Inspector relies on every rule being inserted in the normal level
            // at least once, in order to return the rules with the correct
            // specificity order.
            //
            // TODO(emilio): If we want to apply these optimizations without
            // breaking inspector's expectations, we'd need to run
            // selector-matching again at the inspector's request. That may or
            // may not be a better trade-off.
            if level.origin() == CascadeOrigin::Transitions && found_important {
                // There can be at most one transition, and it will come at
                // the end of the iterator. Stash it and apply it after
                // !important rules.
                debug_assert!(transition.is_none());
                transition = Some(source);
            } else {
                current = current.ensure_child(self.root(), source, priority);
            }
        }

        // Early-return in the common case of no !important declarations.
        if !found_important {
            return current;
        }

        // Insert important declarations, in order of increasing importance,
        // followed by any transition rule.
        //
        // Important rules are sorted differently from unimportant ones by
        // shadow order and cascade order.
        if !important_author.is_empty()
            && important_author.first().unwrap().1 != important_author.last().unwrap().1
        {
            // We only need to sort if the important rules come from
            // different trees, but we need this sort to be stable.
            //
            // FIXME(emilio): This could maybe be smarter, probably by chunking
            // the important rules while inserting, and iterating the outer
            // chunks in reverse order.
            //
            // That is, if we have rules with levels like: -1 -1 -1 0 0 0 1 1 1,
            // we're really only sorting the chunks, while keeping elements
            // inside the same chunk already sorted. Seems like we could try to
            // keep a SmallVec-of-SmallVecs with the chunks and just iterate the
            // outer in reverse.
            important_author.sort_by_key(|&(_, priority)| priority);
        }

        for (source, priority) in important_author.drain(..) {
            current = current.ensure_child(self.root(), source, priority);
        }

        for (source, priority) in important_user.drain(..) {
            current = current.ensure_child(self.root(), source, priority);
        }

        if !crate::pref!("layout.css.transitions-override-ua")
            && let Some(source) = transition.take()
        {
            current = current.ensure_child(self.root(), source, CascadePriority::TRANSITIONS);
        }

        for (source, priority) in important_ua.drain(..) {
            current = current.ensure_child(self.root(), source, priority);
        }

        if let Some(source) = transition {
            current = current.ensure_child(self.root(), source, CascadePriority::TRANSITIONS);
        }

        current
    }

    /// Given a list of applicable declarations, insert the rules and return the
    /// corresponding rule node.
    pub fn compute_rule_node(
        &self,
        applicable_declarations: &mut ApplicableDeclarationList,
        guards: &StylesheetGuards,
    ) -> StrongRuleNode {
        let mut prev = None;
        self.insert_ordered_rules_with_important(
            applicable_declarations.drain(..).filter_map(|d| {
                // for_rule_tree keeps only these two, so adjacent entries that
                // agree on them contribute the same thing to the rule path.
                let key = (d.source.key(), d.cascade_priority);
                if prev == Some(key) {
                    return None;
                }
                prev = Some(key);
                Some(d.for_rule_tree())
            }),
            guards,
        )
    }

    /// Insert the given rules, that must be in proper order by specifity, and
    /// return the corresponding rule node representing the last inserted one.
    pub fn insert_ordered_rules<'a, I>(&self, iter: I) -> StrongRuleNode
    where
        I: Iterator<Item = (StyleSourceBorrow<'a>, CascadePriority)>,
    {
        self.insert_ordered_rules_from(self.root().clone(), iter)
    }

    fn insert_ordered_rules_from<'a, I>(&self, from: StrongRuleNode, iter: I) -> StrongRuleNode
    where
        I: Iterator<Item = (StyleSourceBorrow<'a>, CascadePriority)>,
    {
        let mut current = from;
        for (source, priority) in iter {
            current = current.ensure_child(self.root(), source, priority);
        }
        current
    }

    /// Replaces the pres hints level.
    pub fn replace_pres_hints(
        &self,
        hints: &[ApplicableDeclarationBlock],
        path: &mut StrongRuleNode,
    ) {
        let result = self.replace_rules_at_level(
            path,
            CascadePriority::PRES_HINTS,
            |n| n.cascade_priority() == CascadePriority::PRES_HINTS,
            hints
                .iter()
                .rev()
                .map(|d| (d.source.to_owned(), d.cascade_priority)),
        );
        debug_assert!(
            !result.important_changed,
            "Pres hints shouldn't have important rules"
        );
        if let Some(new_path) = result.new_path {
            *path = new_path;
        }
    }

    /// Replaces a rule in a given level (if present) for another rule, returning whether any
    /// important rules may have changed.
    pub fn replace_single_rule_at_level(
        &self,
        level: CascadeLevel,
        layer_order: LayerOrder,
        pdb: Option<ArcBorrow<Locked<PropertyDeclarationBlock>>>,
        path: &mut StrongRuleNode,
    ) -> bool {
        let priority = CascadePriority::new(level, layer_order, RuleCascadeFlags::empty());
        let need_insert = std::cell::Cell::new(true);
        let should_remove = |node: &StrongRuleNode| {
            if node.cascade_priority() != priority {
                return false;
            }
            if let Some(ref pdb) = pdb {
                let current_decls = node.style_source().unwrap().get();
                if ArcBorrow::ptr_eq(pdb, &current_decls.borrow_arc()) {
                    need_insert.set(false);
                    return false;
                }
            }
            true
        };
        let new_rule = std::iter::from_fn(|| {
            if !need_insert.replace(false) {
                return None;
            }
            Some((StyleSource::from_declarations(pdb?.clone_arc()), priority))
        });
        let result = self.replace_rules_at_level(path, priority, should_remove, new_rule);
        if let Some(new_path) = result.new_path {
            *path = new_path;
        }
        result.important_changed
    }

    /// Returns whether this rule node has any @starting-style rule.
    pub fn has_starting_style(path: &StrongRuleNode) -> bool {
        path.self_and_ancestors().any(|node| {
            node.cascade_priority()
                .flags()
                .intersects(RuleCascadeFlags::STARTING_STYLE)
        })
    }

    /// Returns new rule nodes without transitions level rule.
    pub fn remove_transition_rule_if_applicable(
        &self,
        path: &StrongRuleNode,
    ) -> Option<StrongRuleNode> {
        self.replace_rules_at_level(
            path,
            CascadePriority::TRANSITIONS,
            |n| n.cascade_priority() == CascadePriority::TRANSITIONS,
            std::iter::empty(),
        )
        .new_path
    }

    /// Returns a rule node with all the rules for which should_remove returns true removed, and any
    /// new rules (in descending cascade order) inserted at insert_priority.
    ///
    /// Assumes anything lower than insert_priority should not be looked at.
    fn replace_rules_at_level(
        &self,
        path: &StrongRuleNode,
        insert_priority: CascadePriority,
        mut should_remove: impl FnMut(&StrongRuleNode) -> bool,
        new_level_children: impl Iterator<Item = (StyleSource, CascadePriority)>,
    ) -> RuleReplacementResult {
        let mut last = None;
        let iter = path
            .self_and_ancestors()
            .take_while(|node| node.cascade_priority() >= insert_priority);
        let mut changed = false;
        let mut children = SmallVec::<[_; 10]>::new();
        let mut important_changed = false;
        for node in iter {
            let prio = node.cascade_priority();
            if !should_remove(node) {
                children.push((node.style_source().unwrap().clone(), prio));
            } else {
                important_changed |= prio.is_important();
                changed = true;
            }
            last = Some(node);
        }

        for (new_child_source, new_child_prio) in new_level_children {
            children.push((new_child_source, new_child_prio));
            important_changed |= new_child_prio.is_important();
            changed = true;
        }

        let new_path = changed.then(|| {
            let from = match last {
                Some(last) => last.parent().unwrap().clone(),
                None => path.clone(),
            };
            self.insert_ordered_rules_from(
                from,
                children.iter().rev().map(|(s, p)| (s.borrow(), *p)),
            )
        });

        RuleReplacementResult {
            new_path,
            important_changed,
        }
    }

    /// Returns new rule node without rules from declarative animations.
    pub fn remove_animation_rules(&self, path: &StrongRuleNode) -> Option<StrongRuleNode> {
        self.replace_rules_at_level(
            path,
            CascadePriority::SMIL,
            |n| n.cascade_level().is_animation(),
            std::iter::empty(),
        )
        .new_path
    }
}

impl StrongRuleNode {
    /// Get an iterator for this rule node and its ancestors.
    pub fn self_and_ancestors(&self) -> SelfAndAncestors<'_> {
        SelfAndAncestors {
            current: Some(self),
        }
    }

    /// Get a set of properties whose CascadeLevel are higher than Animations
    /// but not equal to Transitions.
    ///
    /// If there are any custom properties, we set the boolean value of the
    /// returned tuple to true.
    pub fn get_properties_overriding_animations(
        &self,
        guards: &StylesheetGuards,
    ) -> (LonghandIdSet, bool) {
        use crate::properties::PropertyDeclarationId;

        // We want to iterate over cascade levels that override the animations
        // level, i.e.  !important levels and the transitions level.
        //
        // However, we actually want to skip the transitions level because
        // although it is higher in the cascade than animations, when both
        // transitions and animations are present for a given element and
        // property, transitions are suppressed so that they don't actually
        // override animations.
        let iter = self
            .self_and_ancestors()
            .take_while(|node| node.cascade_level() > CascadeLevel::new(CascadeOrigin::Animations));
        let mut result = (LonghandIdSet::new(), false);
        for node in iter {
            if node.cascade_level().is_animation() {
                // Note that this skips transitions too.
                continue;
            }
            let style = node.style_source().unwrap();
            for (decl, important) in style
                .read(node.cascade_level().guard(guards))
                .declaration_importance_iter()
            {
                // Although we are only iterating over cascade levels that
                // override animations, in a given property declaration block we
                // can have a mixture of !important and non-!important
                // declarations but only the !important declarations actually
                // override animations.
                if important.important() {
                    match decl.id() {
                        PropertyDeclarationId::Longhand(id) => result.0.insert(id),
                        PropertyDeclarationId::Custom(_) => result.1 = true,
                    }
                }
            }
        }
        result
    }
}

/// An iterator over a rule node and its ancestors.
#[derive(Clone)]
pub struct SelfAndAncestors<'a> {
    current: Option<&'a StrongRuleNode>,
}

impl<'a> Iterator for SelfAndAncestors<'a> {
    type Item = &'a StrongRuleNode;

    fn next(&mut self) -> Option<Self::Item> {
        self.current.inspect(|node| {
            self.current = node.parent();
        })
    }
}
