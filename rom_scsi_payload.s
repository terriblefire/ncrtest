
	xdef @Launch
	xdef _ncr_scsi_cli
	xdef _end

	section	CODE

@Launch	movem.l	d2-d7/a2-a6,-(sp)
	jsr	(a1)
	movem.l	(sp)+,d2-d7/a2-a6
	rts

	cnop 0,4

_ncr_scsi_cli
	incbin ncr_scsi

_end:
