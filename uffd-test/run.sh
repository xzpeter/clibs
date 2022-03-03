modes="missing wp"
mems="anon shmem hugetlb hugetlb_shared"

if [[ "$(whoami)" != root ]]; then
	echo "Please run this using root or with sudo prefix!"
	exit -1
fi

make || exit -1

for mode in $modes; do
	for mem in $mems; do
		echo -n "Running test with mode=$mode and mem=$mem..."
		./uffd-test $mode $mem &> log
        if [[ $? == 0 ]]; then
            echo "done"
        else
            echo "failed"
            cat log
            exit -1
        fi
	done
done
