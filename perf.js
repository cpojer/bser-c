'use strict';

const assert = require('assert');
const fs = require('fs');
const bser = require('bser');
const bserc = require('./');

const jsonData = fs.readFileSync('./perf/jest-cache.json', 'utf-8');
const bserData = fs.readFileSync('./perf/jest-cache.bser');

function measure(callback, times = 10) {
  let elapsed = 0;
  for (var i = 0; i < times; i++) {
    const start = Date.now();
    callback();
    elapsed = Date.now() - start;
  }
  return elapsed / times;
};


console.log('json', measure(() => JSON.parse(jsonData)));
console.log('bserc', measure(() => bserc.loads(bserData)));
console.log('bser', measure(() => bser.loadFromBuffer(bserData)));

assert.equal(
  JSON.stringify(bserc.loads(bserData)),
  JSON.stringify(JSON.parse(jsonData))
);
