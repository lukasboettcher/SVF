#  !bash
# type './build.sh'  for release build
# type './build.sh debug'  for debug build

#########
# VARs and Links
########
SVFHOME=$(pwd)
sysOS=`uname -s`
MacLLVM="https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.0/clang+llvm-10.0.0-x86_64-apple-darwin.tar.xz"
UbuntuLLVM="https://github.com/llvm/llvm-project/releases/download/llvmorg-10.0.0/clang+llvm-10.0.0-x86_64-linux-gnu-ubuntu-18.04.tar.xz"
MacZ3="https://github.com/Z3Prover/z3/releases/download/z3-4.8.8/z3-4.8.8-x64-osx-10.14.6.zip"
UbuntuZ3="https://github.com/Z3Prover/z3/releases/download/z3-4.8.8/z3-4.8.8-x64-ubuntu-16.04.zip"
MacCTIR="https://github.com/mbarbar/ctir/releases/download/ctir-10.c2/ctir-clang-v10.c2-macos10.15.zip"
UbuntuCTIR="https://github.com/mbarbar/ctir/releases/download/ctir-10.c2/ctir-clang-v10.c2-ubuntu18.04.zip"
LLVMHome="llvm-10.0.0.obj"
Z3Home="z3.obj"
CTIRHome="ctir.obj"
SVFTests="Test-Suite"

# Downloads $1 to $2.
function generic_dl {
    if [ $# -ne 2 ]
    then
        echo "$0: bad args to dl_file!"
        exit 1
    fi

    if [[ "$sysOS" == "Darwin" ]]
    then
        curl -L "$1" > "$2"
    elif [[ "$sysOS" == "Linux" ]]
    then
        wget -c "$1" -O "$2"
    else
        echo "Cannot download file for non-Darwin/-Linux."
    fi
}

# OS-specific values.
urlLLVM=""
urlZ3=""
urlCTIR=""
OSDisplayName=""

if [[ $sysOS == "Darwin" ]]
then
    urlLLVM="$MacLLVM"
    urlZ3="$MacZ3"
    urlCTIR="$MacCTIR"
    OSDisplayName="macOS"
elif [[ $sysOS == "Linux" ]]
then
    urlLLVM="$UbuntuLLVM"
    urlZ3="$UbuntuZ3"
    urlCTIR="$UbuntuCTIR"
    OSDisplayName="Ubuntu"
else
    echo "Builds outside Ubuntu and macOS are not supported."
fi

if [ ! -d "$LLVM_DIR" ]
then
    if [ ! -d "$LLVMHome" ]
    then
        echo "Downloading LLVM binary for $OSDisplayName"
        generic_dl "$urlLLVM" llvm.tar.xz
        mkdir -p "./$LLVMHome" && tar -xf llvm.tar.xz -C "./$LLVMHome" --strip-components 1
        rm llvm.tar.xz
    fi

    export LLVM_DIR="$SVFHOME/$LLVMHome"
fi

if [ ! -d "$Z3_DIR" ]
then
    if [ ! -d "$Z3Home" ]
    then
        echo "Downloading Z3 binary for $OSDisplayName"
        generic_dl "$urlZ3" z3.zip
        unzip -q "z3.zip" && mv ./z3-* ./$Z3Home
        rm z3.zip
    fi

    export Z3_DIR="$SVFHOME/$Z3Home"
fi

if [ ! -d "$CTIR_DIR" ]
then
    if [ ! -d "$CTIRHome" ]
    then
        echo "Downloading ctir Clang binary for $OSDisplayName"
        generic_dl "$urlCTIR" ctir.zip
        mkdir -p "$CTIRHome" && unzip -q "ctir.zip" -d "$CTIRHome"
        rm ctir.zip
    fi

    export CTIR_DIR="$SVFHOME/$CTIRHome"
fi

export PATH=$LLVM_DIR/bin:$PATH
echo "LLVM_DIR =" $LLVM_DIR

########
# Download SVF Test Suite
#######

if [ ! -d "$SVFTests" ]
then
   git clone "https://github.com/SVF-tools/Test-Suite.git"
   if [[ $sysOS == "Linux" ]] ; then
   	cd ./$SVFTests
   	./generate_bc.sh
   	cd ..
   fi
fi


########
# Build SVF
########
if [[ $1 == 'debug' ]]
then
	rm -rf ./'Debug-build'
	mkdir ./'Debug-build'
	cd ./'Debug-build'
	cmake -D CMAKE_BUILD_TYPE:STRING=Debug ../
else
	rm -rf ./'Release-build'
	mkdir ./'Release-build'
	cd ./'Release-build'
	cmake ../
fi
make -j4

########
# Set up environment variables of SVF
########
cd ../
if [[ $1 == 'debug' ]]
then
  . ./setup.sh debug
else
  . ./setup.sh
fi

#########
# Run ctest
########
if [[ $sysOS == "Linux" ]]  
then
   if [[ $1 == 'debug' ]]
   then
     cd ./'Debug-build'
   else 
     cd ./'Release-build'
   fi
  ctest --v
fi

#########
# Optionally, you can also specify a CXX_COMPILER and your $LLVM_HOME for your build
# cmake -DCMAKE_CXX_COMPILER=$LLVM_DIR/bin/clang++ -DLLVM_DIR=$LLVM_DIR
#########
