Set PlaybackSpeed 4.0
Set LoopOffset 50% 
Set TypingSpeed 0.6
Set Margin 0
Set Padding 5
Set WindowBar Colorful
Output demo.gif
Set CursorBlink false
Set FontSize 14
Set Width 1000
Set Height 500
Hide
Type "export PATH=$PATH:/stsmon"
Enter
Type "test-tsg > /dev/null &"
Enter
Type "clear"
Enter
Show
Type "stsmon -m 239.239.42.12 -c"
Enter
Sleep 35s
Ctrl+c
Sleep 10s
Hide
Ctrl+f
Ctrl+c