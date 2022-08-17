set i 0
for {set i 0} {$i<1000} {incr i} {
	exec touch $i;
	exec echo hi $i >$i
	exec rm $i
}
incr i
puts "$i"
