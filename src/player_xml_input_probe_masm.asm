; The only qualified native call leaves the allocated file length in RBX.
; Leaf entry: preserve stack, nonvolatile registers, and the original return
; address while forwarding that observed length as the fourth ABI argument.
OPTION CASEMAP:NONE
EXTERN dluma_player_xml_input_parse_hook:PROC
PUBLIC dluma_player_xml_input_parse_probe
.code
dluma_player_xml_input_parse_probe PROC
    mov r9, rbx
    jmp dluma_player_xml_input_parse_hook
dluma_player_xml_input_parse_probe ENDP
END
