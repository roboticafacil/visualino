// Do not edit this file; automatically generated by build.py.
"use strict";Blockly.Msg.DUPLICATE_BLOCK="Duplicate",Blockly.Msg.REMOVE_COMMENT="Remove Comment",Blockly.Msg.ADD_COMMENT="Add Comment",Blockly.Msg.EXTERNAL_INPUTS="External Inputs",Blockly.Msg.INLINE_INPUTS="Inline Inputs",Blockly.Msg.DELETE_BLOCK="Delete Block",Blockly.Msg.DELETE_X_BLOCKS="Delete %1 Blocks",Blockly.Msg.COLLAPSE_BLOCK="Collapse Block",Blockly.Msg.EXPAND_BLOCK="Expand Block",Blockly.Msg.DISABLE_BLOCK="Disable Block",Blockly.Msg.ENABLE_BLOCK="Enable Block",Blockly.Msg.HELP="Help",Blockly.Msg.COLLAPSE_ALL="Collapse Blocks",Blockly.Msg.EXPAND_ALL="Expand Blocks",Blockly.Arduino=new Blockly.Generator("Arduino"),Blockly.Arduino.RESERVED_WORDS_||(Blockly.Arduino.RESERVED_WORDS_=""),Blockly.Arduino.RESERVED_WORDS_+="setup,loop,if,else,for,switch,case,while,do,break,continue,return,goto,define,include,HIGH,LOW,INPUT,OUTPUT,INPUT_PULLUP,true,false,interger, constants,floating,point,void,boolean,char,unsigned,byte,int,word,long,float,double,string,String,array,static, volatile,const,sizeof,pinMode,digitalWrite,digitalRead,analogReference,analogRead,analogWrite,tone,noTone,shiftOut,shitIn,pulseIn,millis,micros,delay,delayMicroseconds,min,max,abs,constrain,map,pow,sqrt,sin,cos,tan,randomSeed,random,lowByte,highByte,bitRead,bitWrite,bitSet,bitClear,bit,attachInterrupt,detachInterrupt,interrupts,noInterrupts",Blockly.Arduino.ORDER_ATOMIC=0,Blockly.Arduino.ORDER_UNARY_POSTFIX=1,Blockly.Arduino.ORDER_UNARY_PREFIX=2,Blockly.Arduino.ORDER_MULTIPLICATIVE=3,Blockly.Arduino.ORDER_ADDITIVE=4,Blockly.Arduino.ORDER_SHIFT=5,Blockly.Arduino.ORDER_RELATIONAL=6,Blockly.Arduino.ORDER_EQUALITY=7,Blockly.Arduino.ORDER_BITWISE_AND=8,Blockly.Arduino.ORDER_BITWISE_XOR=9,Blockly.Arduino.ORDER_BITWISE_OR=10,Blockly.Arduino.ORDER_LOGICAL_AND=11,Blockly.Arduino.ORDER_LOGICAL_OR=12,Blockly.Arduino.ORDER_CONDITIONAL=13,Blockly.Arduino.ORDER_ASSIGNMENT=14,Blockly.Arduino.ORDER_NONE=99;var profile={arduino:{description:"Arduino standard-compatible board",digital:[["1","1"],["2","2"],["3","3"],["4","4"],["5","5"],["6","6"],["7","7"],["8","8"],["9","9"],["10","10"],["11","11"],["12","12"],["13","13"],["A0","A0"],["A1","A1"],["A2","A2"],["A3","A3"],["A4","A4"],["A5","A5"]],analog:[["A0","A0"],["A1","A1"],["A2","A2"],["A3","A3"],["A4","A4"],["A5","A5"]],serial:9600},arduino_mega:{description:"Arduino Mega-compatible board"}};profile.default=profile.arduino,Blockly.Arduino.init=function(){Blockly.Arduino.definitions_={},Blockly.Arduino.setups_={},Blockly.Arduino.loops_={},Blockly.Arduino.int0_={},Blockly.Variables&&(Blockly.Arduino.variableDB_?Blockly.Arduino.variableDB_.reset():Blockly.Arduino.variableDB_=new Blockly.Names(Blockly.Arduino.RESERVED_WORDS_))},Blockly.Arduino.finish=function(n){n="void loop() \n{\n"+(n=(n="  "+n.replace(/\n/g,"\n  ")).replace(/\n\s+$/,"\n"))+"\n}";var o,l=[],e=[],i=[],r=[],t=[],c=[],h=[];for(o in Blockly.Arduino.definitions_)i=Blockly.Arduino.definitions_[o],i.match(/^#include/)?l.push(i):0<=o.search("declare_var")?r.push(i):0<=o.search("define_class")?h.push(i):0<=o.search("define_task")?c.push(i):0<=o.search("define_backgroundtask")?c.push(i):0<=o.search("define_isr")?t.push(i):e.push(i);i=Blockly.Arduino.orderDefinitions(e),e=[];for(o in Blockly.Arduino.setups_)e.push(Blockly.Arduino.setups_[o]);return n=(l.join("\n")+"\n\n/***   Global variables   ***/\n"+r.join("")+"\n\n/***   Function declaration   ***/\n"+i[0]+"\n\n/***   Class declaration   ***/\n"+h.join("")+"\n\n/***   Tasks declaration   ***/\n"+c.join("")+"\n\n/***   ISR function declaration   ***/\n"+t.join("")+"\nvoid setup() \n{\n  "+e.join("\n  ")+"\n}\n\n").replace(/\n\n+/g,"\n\n").replace(/\n*$/,"\n\n\n")+n+"\n\n/***   Function definition   ***/\n"+i[1],n=n.replace(/&quot;/g,'"'),n=n.replace(/&amp;quot;/g,'"'),n=n.replace(/quot;/g,'"'),n=n.replace(/&amp;/g,"&"),n=n.replace(/amp;/g,""),n=n.replace(/&lt;/g,"<"),n=n.replace(/lt;/g,"<"),n=n.replace(/&gt;/g,">"),n=n.replace(/gt;/g,">")},Blockly.Arduino.orderDefinitions=function(n){var o,l=[],e=[];for(o in n){var i=n[o].substring(0,n[o].search("\\)")+1);i.replace("\n",""),""!==i&&(i+=";\n",l+=i,e+=n[o])}return[l,e]},Blockly.Arduino.scrubNakedValue=function(n){return n+";\n"},Blockly.Arduino.quote_=function(n){return'"'+(n=n.replace(/\\/g,"\\\\").replace(/\n/g,"\\\n").replace(/\$/g,"\\$").replace(/'/g,"\\'"))+'"'},Blockly.Arduino.scrub_=function(n,o){if(null===o)return"";var l="";if(!n.outputConnection||!n.outputConnection.targetConnection){var e=n.getCommentText();e&&(l+=Blockly.Generator.prefixLines(e,"// ")+"\n");for(var i=0;i<n.inputList.length;i++)n.inputList[i].type==Blockly.INPUT_VALUE&&(e=n.inputList[i].connection.targetBlock())&&(e=Blockly.Generator.allNestedComments(e))&&(l+=Blockly.Generator.prefixLines(e,"// "))}return i=n.nextConnection&&n.nextConnection.targetBlock(),i=this.blockToCode(i),l+o+i},Blockly.Generator.prefixLines=function(n,o){return o+n.replace(/\n(.)/g,"\n"+o+"$1")},Blockly.Generator.allNestedComments=function(n){var o=[];n=n.getDescendants();for(var l=0;l<n.length;l++){var e=n[l].getCommentText();e&&o.push(e)}return o.length&&o.push(""),o.join("\n")};