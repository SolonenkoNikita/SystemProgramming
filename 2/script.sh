mkdir -p testdir
cd testdir
../mybash << 'EOF'
pwd | tail -c 8
EOF