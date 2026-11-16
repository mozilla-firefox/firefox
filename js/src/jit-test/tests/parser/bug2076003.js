// |jit-test| skip-if: isLcovEnabled()

const labels = [];
for (let value = 0; value <= 32766; value += 2) {
  labels.push('case ' + value + ':');
}
const fullSwitch = 'switch(discriminant){' + labels.join('') + '}';
let body = fullSwitch.repeat(512);
const finalLabels = [];
for (let value = 0; value < 512; value += 2) {
  finalLabels.push('case ' + value + ':');
}
body += 'switch(discriminant){' + finalLabels.join('') + 'case 511:}';
body += 'switch(discriminant){}';
new Function('discriminant', body);
