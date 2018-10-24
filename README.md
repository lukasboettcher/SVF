## News
* <b>SVF now supports LLVM-7.0.0. </b>
* <b>SVF now supports Docker. [Try SVF in Docker](https://github.com/SVF-tools/SVF/wiki/Try-SVF-in-Docker)! </b>
* <b>SVF now supports [LLVM-6.0.0](https://github.com/svf-tools/SVF/pull/38) (Contributed by [Jack Anthony](https://github.com/jackanth)). </b>
* <b>SVF now supports [LLVM-4.0.0](https://github.com/svf-tools/SVF/pull/23) (Contributed by Jared Carlson. Thank [Jared](https://github.com/jcarlson23) and [Will](https://github.com/dtzWill) for their in-depth [discussions](https://github.com/svf-tools/SVF/pull/18) about updating SVF!) </b>
* <b>SVF now supports analysis for C++ programs.</b>
<br />



<br />
<br />
<br />
SVF is a static tool that enables scalable and precise interprocedural dependence analysis for C and C++ programs. SVF allows value-flow construction and pointer analysis to be performed iteratively, thereby providing increasingly improved precision for both. 

SVF accepts the points-to information generated by any pointer analysis (e.g., Andersen’s analysis) and constructs an interprocedural memory SSA form so that the def-use chains of both top-level and address-taken variables are captured. SVF is implemented on top of an industry-strength compiler [LLVM](http://llvm.org) (version 6.0.0). SVF contains a third party software package [CUDD-2.5.0](http://vlsi.colorado.edu/~fabio/CUDD/) (Binary Decision Diagrams (BDDs)), which is used to encode path conditions.

<br />

| About SVF       | Setup  Guide         | User Guide  | Developer Guide  |
| ------------- |:-------------:| -----:|-----:|
| ![About](https://github.com/svf-tools/SVF/blob/master/images/help.png?raw=true)| ![Setup](https://github.com/svf-tools/SVF/blob/master/images/tools.png?raw=true)  | ![User](https://github.com/svf-tools/SVF/blob/master/images/users.png?raw=true)  |  ![Developer](https://github.com/svf-tools/SVF/blob/master/images/database.png?raw=true) 
| Introducing SVF -- [what it does](https://github.com/svf-tools/SVF/wiki/About#what-is-svf) and [how we design it](https://github.com/svf-tools/SVF/wiki/SVF-Design#svf-design)      | A step by step [setup guide](https://github.com/svf-tools/SVF/wiki/Setup-Guide#getting-started) to build SVF | Command-line options to [run SVF](https://github.com/svf-tools/SVF/wiki/User-Guide#quick-start), get [analysis outputs](https://github.com/svf-tools/SVF/wiki/User-Guide#analysis-outputs), and test SVF with [an example](https://github.com/svf-tools/SVF/wiki/Analyze-a-Simple-C-Program) or [PTABen](https://github.com/SVF-tools/PTABen) | Detailed [technical documentation](https://github.com/svf-tools/SVF/wiki/Technical-documentation) and how to [write your own analyses](https://github.com/svf-tools/SVF/wiki/Write-your-own-analysis-in-SVF) on top of SVF |


<br />
<br />
<p>We release SVF source code in the hope of benefiting others. You are kindly asked to acknowledge usage of the tool by citing some of our publications listed http://svf-tools.github.io/SVF, especially the following two: </p>

Yulei Sui and Jingling Xue. [SVF: Interprocedural Static Value-Flow Analysis in LLVM](https://yuleisui.github.io/publications/cc16.pdf) Compiler Construction (CC '16) 

Yulei Sui, Ding Ye, and Jingling Xue. [Detecting Memory Leaks Statically with Full-Sparse Value-Flow Analysis](https://yuleisui.github.io/publications/tse14.pdf) , IEEE Transactions on Software Engineering (TSE'14) 

<br />





