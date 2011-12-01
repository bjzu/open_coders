/*-----------------------------------------------------------------------------
 *  encoders.cpp - Encoders implementing many types of copression algorithms.
 *
 *  Coding-Style:
 *      emacs) Mode: C, tab-width: 8, c-basic-offset: 8, indent-tabs-mode: nil
 *      vi) tabstop: 8, expandtab
 *
 *  Authors:
 *      Takeshi Yamamuro <linguin.m.s_at_gmail.com>
 *      Fabrizio Silvestri <fabrizio.silvestri_at_isti.cnr.it>
 *      Rossano Venturini <rossano.venturini_at_isti.cnr.it>
 *-----------------------------------------------------------------------------
 */

#include "encoders.hpp"

using namespace std;

#define __header_written(out)   \
        do {                    \
                uint32_t        magic;  \
                uint32_t        vmajor; \
                uint32_t        vminor; \
\
                magic = MAGIC_NUM;      \
                vmajor = VMAJOR;        \
                vminor = VMINOR;        \
\
                fwrite(&magic, sizeof(uint32_t), 1, out);       \
                fwrite(&vmajor,sizeof(uint32_t), 1, out);       \
                fwrite(&vminor,sizeof(uint32_t), 1, out);       \
        } while (0)

static void __usage(const char *msg, ...);

int 
main(int argc, char **argv)
{
        int             encID;
        uint32_t        i;
        uint32_t        *list;
        uint32_t        *cmp_array;
        uint32_t        *addr;
        uint64_t        fsz;
        uint64_t        lenmax;
        char            ifile[NFILENAME];
        char            ofile[NFILENAME + NEXTNAME];
        char            *end;
        FILE            *cmp;
        FILE            *toc;

        if (argc < 3)
                __usage(NULL);

        list = new uint32_t[MAXLEN + TAIL_MERGIN];
        cmp_array = new uint32_t[MAXLEN + TAIL_MERGIN];

        if (list == NULL || cmp_array == NULL)
                eoutput("Can't allocate memory");

        /* Read EncoderID */
        encID = strtol(argv[1], &end, 10);
        if ((*end != '\0') || (encID < 0) ||
                        (encID >= NUMENCODERS) ||(errno == ERANGE))
                __usage("EncoderID '%s' invalid", argv[1]);

        /* Read file name */
        strncpy(ifile, argv[2], NFILENAME);
        ifile[NFILENAME - 1] = '\0';

        /* Open a output file and tune buffer mode */
        strncpy(ofile, ifile, NFILENAME);
        strcat(ofile, enc_ext[encID]);
        cmp = fopen(ofile, "w");

        strcat(ofile, TOCEXT);
        toc = fopen(ofile, "w");

        if (cmp == NULL || toc == NULL)
                eoutput("foepn(): Can't create a output file");

        setvbuf(cmp, NULL, _IOFBF, BUFSIZ);
        setvbuf(toc, NULL, _IOFBF, BUFSIZ);

        /* First off, a header is written */
        __header_written(toc);

        /*
         * FIXME: I think loops with mmap() is faster than that with
         * xxxread() on most linux platforms. True?
         */
        addr = int_utils::open_and_mmap_file(ifile, false, fsz);
        lenmax = fsz >> 2;

        {
                uint32_t        prev_doc;
                uint32_t        cur_doc;
                uint32_t        cmp_pos;
                uint32_t        cmp_size;
                uint32_t        num;
                uint64_t        len;

                for (len = 0, cmp_pos = 0; len < lenmax; ) {
                        /* Read the numer of integers in a list */
                        num = __next_read(addr, len);

                        if (len + num > lenmax)
                                goto LOOP_END;

                        /* Read the head of a list */
                        prev_doc = __next_read(addr, len);

                        if (num > SKIP) {
                                /*
                                 * For any list, TOC will contain:
                                 *      (number of elements, first elements, pointer to the compressed list)
                                 */
                                fwrite(&num, 1, sizeof(uint32_t), toc);
                                fwrite(&prev_doc, 1, sizeof(uint32_t), toc);
                                fwrite(&cmp_pos, 1, sizeof(uint32_t), toc);

                                for (i = 0; i < num - 1; i++) {
                                        cur_doc = __next_read(addr, len);

                                        if (cur_doc < prev_doc)
                                                cerr << "List ordering exception: list MUST be increasing" << endl;

                                        if (encID != E_BINARYIPL)
                                                list[i] = cur_doc - prev_doc - 1;
                                        else
                                                list[i] = cur_doc;

                                        prev_doc = cur_doc;
                                }

                                /* Do encoding */
                                (encoders[encID])(list, num - 1, cmp_array, cmp_size);

                                fwrite(cmp_array, sizeof(uint32_t), cmp_size, cmp);
                                cmp_pos += cmp_size;
                        } else {
                                for (i = 0; i < num - 1; i++) {
                                        cur_doc = __next_read(addr, len);

                                        if (cur_doc <= prev_doc)
                                                cerr << "List ordering exception: list MUST be increasing" << endl;

                                        if (encID != 5)
                                                list[i] = cur_doc - prev_doc - 1;
                                        else
                                                list[i] = cur_doc;

                                        prev_doc = cur_doc;
                                }
                        }
                }
        }
LOOP_END:

        /* Finalization */
        int_utils::close_file(addr, fsz);

        fclose(cmp);
        fclose(toc);

        delete[] list;
        delete[] cmp_array;

        return EXIT_SUCCESS;
}

/*--- Intra functions below ---*/

void
__usage(const char *msg, ...)
{
        cout << "Usage: encoders <EncoderID> <infilename>" << endl;

        if (msg != NULL) {
                va_list vargs;

                va_start(vargs, msg);
                vfprintf(stdout, msg, vargs);
                va_end(vargs);

                cout << endl;
        }

        cout << endl << "EncoderID\tEncoderName" << endl;
        cout << "---" << endl;

        cout << "\t0\tGamma" << endl;
        cout << "\t1\tDelta" << endl;
        cout << "\t2\tVariable Byte" << endl;
        cout << "\t3\tBinary Interpolative" << endl;
        cout << "\t4\tSimple 9" << endl;
        cout << "\t5\tSimple 16" << endl;
        cout << "\t6\tPForDelta" << endl; 
        cout << "\t7\tOPTPForDelta" << endl; 
        cout << "\t8\tVSEncodingBlocks" << endl;
        cout << "\t9\tVSE-R" << endl;
        cout << "\t10\tVSEncodingRest" << endl;
        cout << "\t11\tVSEncodingBlocksHybrid" << endl;
        cout << "\t12\tVSEncodingSimple v1" << endl;
        cout << "\t13\tVSEncodingSimple v2" << endl << endl;

        exit(1);
}
