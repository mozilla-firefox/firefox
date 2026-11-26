/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * https://webaudio.github.io/web-audio-api/#audioworkletglobalscope
 * https://w3c.github.io/hr-time/#sec-audioworkletglobalscope
 */

callback constructor AudioWorkletProcessorConstructor = AudioWorkletProcessor (object options);

[Global=(Worklet,AudioWorklet),Exposed=AudioWorklet]
interface AudioWorkletGlobalScope : WorkletGlobalScope {
    [Throws]
    undefined registerProcessor (DOMString name, AudioWorkletProcessorConstructor processorCtor);
    readonly  attribute   unsigned long long currentFrame;
    readonly  attribute   double currentTime;
    readonly  attribute   float sampleRate;
    readonly attribute MessagePort port;
    [Constant, Replaceable]
    readonly attribute WorkletPerformance performance;
};
