// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

// Management of the peer's ack rate.

use std::{cmp::max, time::Duration};

use neqo_common::Buffer;
use tracing::trace;

use crate::{
    connection::params::ConnectionParameters, frame::FrameType, packet, recovery,
    stats::FrameStats, tracking::DEFAULT_REMOTE_ACK_DELAY,
};

#[derive(Debug, Clone, Copy)]
pub struct AckRate {
    /// The maximum number of packets that can be received without sending an ACK.
    packets: u16,
    /// The maximum delay before sending an ACK, in microseconds.
    delay: u32,
}

impl AckRate {
    fn delay_us(delay: Duration) -> u32 {
        u32::try_from(delay.as_micros()).unwrap_or(u32::MAX)
    }

    pub fn new(minimum: Duration, ratio: u8, cwnd: usize, mtu: usize, rtt: Duration) -> Self {
        const PACKET_RATIO: usize = ConnectionParameters::ACK_RATIO_SCALE as usize;
        // At worst, ask for an ACK for every other packet.
        const MIN_PACKETS: usize = 2;
        // At worst, require an ACK every 256 packets.
        const MAX_PACKETS: usize = 256;
        const RTT_RATIO: u32 = ConnectionParameters::ACK_RATIO_SCALE as u32;
        const MAX_DELAY: Duration = Duration::from_millis(50);

        let packets = cwnd * PACKET_RATIO / mtu / usize::from(ratio);
        let packets = packets.clamp(MIN_PACKETS, MAX_PACKETS);
        let delay = rtt * RTT_RATIO / u32::from(ratio);
        // The peer's minimum takes precedence over our preferred maximum.
        let delay = delay.min(MAX_DELAY).max(minimum);
        trace!("AckRate inputs: {cwnd}/{mtu}/{ratio}, {rtt:?}");
        Self {
            packets: u16::try_from(packets).expect("clamped to MAX_PACKETS"),
            delay: Self::delay_us(delay),
        }
    }

    pub fn write_frame<B: Buffer>(self, builder: &mut packet::Builder<B>, seqno: u64) -> bool {
        builder.write_varint_frame(&[
            u64::from(FrameType::AckFrequency),
            seqno,
            u64::from(self.packets),
            u64::from(self.delay),
            0,
        ])
    }

    /// Determine whether to send an update frame.
    pub const fn needs_update(self, target: Self) -> bool {
        if self.packets != target.packets {
            return true;
        }
        // Allow more flexibility for delays, as those can change
        // by small amounts fairly easily.
        let delta = target.delay / 4;
        target.delay.saturating_add(delta) < self.delay
            || target.delay > self.delay.saturating_add(delta)
    }
}

#[derive(Debug, Clone)]
pub struct FlexibleAckRate {
    current: AckRate,
    target: AckRate,
    next_frame_seqno: u64,
    frame_outstanding: bool,
    min_ack_delay: Duration,
    ratio: u8,
}

impl FlexibleAckRate {
    fn new(
        max_ack_delay: Duration,
        min_ack_delay: Duration,
        ratio: u8,
        cwnd: usize,
        mtu: usize,
        rtt: Duration,
    ) -> Self {
        trace!("FlexibleAckRate: {max_ack_delay:?} {min_ack_delay:?} {ratio}");
        let ratio = max(ConnectionParameters::ACK_RATIO_SCALE, ratio); // clamp it
        Self {
            current: AckRate {
                packets: 2, // The default behaviour is to acknowledge every second packet.
                delay: AckRate::delay_us(max_ack_delay),
            },
            target: AckRate::new(min_ack_delay, ratio, cwnd, mtu, rtt),
            next_frame_seqno: 0,
            frame_outstanding: false,
            min_ack_delay,
            ratio,
        }
    }

    fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if !self.frame_outstanding
            && self.current.needs_update(self.target)
            && self.target.write_frame(builder, self.next_frame_seqno)
        {
            trace!("FlexibleAckRate: write frame {:?}", self.target);
            self.frame_outstanding = true;
            self.next_frame_seqno += 1;
            tokens.push(recovery::Token::AckFrequency(self.target));
            stats.ack_frequency += 1;
        }
    }

    const fn frame_acked(&mut self, acked: AckRate) {
        self.frame_outstanding = false;
        self.current = acked;
    }

    const fn frame_lost(&mut self, _lost: AckRate) {
        self.frame_outstanding = false;
    }

    fn update(&mut self, cwnd: usize, mtu: usize, rtt: Duration) {
        self.target = AckRate::new(self.min_ack_delay, self.ratio, cwnd, mtu, rtt);
        trace!("FlexibleAckRate: {:?} -> {:?}", self.current, self.target);
    }

    fn peer_ack_delay(&self) -> Duration {
        Duration::from_micros(u64::from(max(self.current.delay, self.target.delay)))
    }
}

#[derive(Debug, Clone)]
pub enum PeerAckDelay {
    Fixed(Duration),
    Flexible(FlexibleAckRate),
}

impl PeerAckDelay {
    pub const fn fixed(max_ack_delay: Duration) -> Self {
        Self::Fixed(max_ack_delay)
    }

    pub fn flexible(
        max_ack_delay: Duration,
        min_ack_delay: Duration,
        ratio: u8,
        cwnd: usize,
        mtu: usize,
        rtt: Duration,
    ) -> Self {
        Self::Flexible(FlexibleAckRate::new(
            max_ack_delay,
            min_ack_delay,
            ratio,
            cwnd,
            mtu,
            rtt,
        ))
    }

    pub fn write_frames<B: Buffer>(
        &mut self,
        builder: &mut packet::Builder<B>,
        tokens: &mut recovery::Tokens,
        stats: &mut FrameStats,
    ) {
        if let Self::Flexible(rate) = self {
            rate.write_frames(builder, tokens, stats);
        }
    }

    pub const fn frame_acked(&mut self, r: AckRate) {
        if let Self::Flexible(rate) = self {
            rate.frame_acked(r);
        }
    }

    pub const fn frame_lost(&mut self, r: AckRate) {
        if let Self::Flexible(rate) = self {
            rate.frame_lost(r);
        }
    }

    pub fn max(&self) -> Duration {
        match self {
            Self::Flexible(rate) => rate.peer_ack_delay(),
            Self::Fixed(delay) => *delay,
        }
    }

    pub fn update(&mut self, cwnd: usize, mtu: usize, rtt: Duration) {
        if let Self::Flexible(rate) = self {
            rate.update(cwnd, mtu, rtt);
        }
    }
}

impl Default for PeerAckDelay {
    fn default() -> Self {
        Self::fixed(DEFAULT_REMOTE_ACK_DELAY)
    }
}

#[cfg(test)]
mod tests {
    use super::{AckRate, ConnectionParameters, Duration};

    #[test]
    fn ack_delay_bounds() {
        for (minimum_us, rtt_us, expected_us) in [
            (1_000, 0, 1_000),
            (1_000, 25_000, 25_000),
            (1_000, 100_000, 50_000),
            (50_000, 100_000, 50_000),
            (50_001, 25_000, 50_001),
            (60_000, 100_000, 60_000),
            (16_383_000, 100_000, 16_383_000),
        ] {
            let rate = AckRate::new(
                Duration::from_micros(minimum_us),
                ConnectionParameters::ACK_RATIO_SCALE,
                12_000,
                1_200,
                Duration::from_micros(rtt_us),
            );
            assert_eq!(
                rate.delay, expected_us,
                "minimum={minimum_us}us, RTT={rtt_us}us"
            );
        }
    }
}
