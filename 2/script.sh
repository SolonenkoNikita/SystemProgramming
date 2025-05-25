mkdir -p testdir
cd testdir
../mybash << 'EOF'
mkdir tmp
cd tmp
touch tmpfile
ls
cd ..
ls
cd tmp
rm tmpfile
cd ..
rmdir tmp
ls
EOF