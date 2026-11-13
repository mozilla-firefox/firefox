// |jit-test| skip-if: helperThreadCount() === 0

evalInWorker("sleep(1);");
evalInWorker(`
with ({}) {}
var target=Function(Array.from({length:65533},(_,i)=>"p"+i).join(","),"return 1");
var body="'use strict'; var x=this; var ion=+inIon();";
for(var i=0;i<1034;i++)body+="var a"+i+"=x*"+(i+2)+";";
body+="Date.now(); return (0";
for(var i=0;i<1034;i++)body+="+a"+i;
body+=")*ion;";
var big=Function(body);
for(var i=0;i<3000000&&big.call(1.5)===0;i++){}
print("big warm",i);
var empty=[];var threshold=-10000000;
var wasIon=false;var stage=0,firstSeen=0,lastSeen=0;
function f(n) {
 stage=1;
 if(n===0)target();
 var first=stackPointerInfo.apply(null,empty);
 for(var i=0;i<n;i++){
   var last=stackPointerInfo.apply(null,empty);
   if(n===1 || first-last>threshold){
     stage=3;firstSeen=first;lastSeen=last;
     wasIon=inIon();return big.apply(1.5,empty);
   }
 }
 return 0;
}
for(var i=0;i<20;i++) f(i%3); threshold=100000; for(var i=0;i<10;i++) f(2);
function rec(n,fn,k){
 if(n>0)return rec(n-1,fn,k);
 return fn(k);
}
var funcs=[stackPointerInfo,Math.abs,Math.round,Math.sqrt,function(x){return x;},function(x){return x+1;},function(x){return x+2;},function(x){return x+3;}];
for(var i=0;i<30000;i++)rec(20,funcs[i&7],0);
print("stack",stackPointerInfo(),rec(0,stackPointerInfo,0),rec(100,stackPointerInfo,0));
threshold=-10000000;for(var i=0;i<50;i++) f(i%3);threshold=100000;for(var i=0;i<50;i++) f(2);
sleep(2);stage=0;
try{print("result",rec(32487,f,10000000));}catch(e){print("caught",e,"stage",stage,firstSeen,lastSeen);}
print("state",stage,firstSeen,lastSeen,wasIon);
`);
