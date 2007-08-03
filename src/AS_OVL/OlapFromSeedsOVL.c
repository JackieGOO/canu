
/**************************************************************************
 * This file is part of Celera Assembler, a software program that 
 * assembles whole-genome shotgun reads into contigs and scaffolds.
 * Copyright (C) 1999-2004, Applera Corporation. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received (LICENSE.txt) a copy of the GNU General Public 
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *************************************************************************/
/*************************************************
* Module:  OlapFromSeeds.c
* Description:
*   Extract from a store exact-match seeds between pairs of
*   reads and use them to determine if the pair actually overlaps.
*   These overlaps are then used to correct errors in reads based
*   on the alignment of all overlapping reads to a given read.
* 
*    Programmer:  A. Delcher
*       Started:  15 February 2007
* 
* Assumptions:
* 
* Notes:
*
*************************************************/

/* RCS info
 * $Id: OlapFromSeedsOVL.c,v 1.9 2007-08-03 14:47:53 brianwalenz Exp $
 * $Revision: 1.9 $
*/

static char CM_ID[] = "$Id: OlapFromSeedsOVL.c,v 1.9 2007-08-03 14:47:53 brianwalenz Exp $";


#include "OlapFromSeedsOVL.h"


int  main
    (int argc, char * argv [])

  {
   FILE  * fp;

   Parse_Command_Line  (argc, argv);

   Now = time (NULL);
   fprintf (stderr, "### Starting at  %s", ctime (& Now));

   Initialize_Globals ();
   fprintf (stderr, "Error rate = %.3f\n", Error_Rate);

   gkpStore = openGateKeeperStore(gkpStore_Path, FALSE);
   assert (gkpStore != NULL);

   fprintf (stderr, "Starting Read_Frags ()\n");
   Read_Frags ();

   fprintf (stderr, "Starting Read_Seeds ()\n");
   Read_Seeds ();

   fprintf (stderr, "Before sort\n");
   qsort (Olap, Num_Olaps, sizeof (Olap_Info_t), By_B_IID);

   if  (Verbose_Level > 2)
       {
        int  i;

        for  (i = 0;  i < Num_Olaps;  i ++)
          printf ("%8d %8d %5d %5d  %c\n",
                  Olap [i] . a_iid, Olap [i] . b_iid,
                  Olap [i] . a_hang, Olap [i] . b_hang,
                  Olap [i] . orient == INNIE ? 'I' : 'N');
       }

   if  (Num_Olaps > 0)
       {
        fprintf (stderr, "Before Stream_Old_Frags  Num_Olaps = %d\n", Num_Olaps);
        if  (Num_PThreads > 0)
            Threaded_Stream_Old_Frags ();
          else
            Stream_Old_Frags ();
        fprintf (stderr, "                   Failed overlaps = %d\n", Failed_Olaps);
       }

   closeGateKeeperStore (gkpStore);

#if USE_NEW_STUFF
   Analyze_Diffs ();
#else
   if (Doing_Corrections)
     {
      if  (Verbose_Level > 1)
        Show_Votes (stdout);

      fprintf (stderr, "Before Output_Corrections  Num_Frags = %d\n", Num_Frags);
      fp = File_Open (Correction_Filename, "wb");
      Output_Corrections (fp);
      fclose (fp);
     }
#endif

   Tidy_Up ();

   Now = time (NULL);
   fprintf (stderr, "### Finished at  %s", ctime (& Now));

   return  0;
  }



static void  Add_Homopoly_Vote
  (Vote_Tally_t * vote, int hp_len)

// Add  hp_len  to the  vote -> homopoly_sum  and increment
//  vote -> homopoly_ct

  {
   if (vote -> homopoly_ct < MAX_VOTE)
     {
      vote -> homopoly_ct ++;
      vote -> homopoly_sum += hp_len;
     }

   return;
  }



static void  Analyze_Alignment
    (int delta [], int delta_len, char * a_part, char * b_part,
     int  a_len, int b_len, int a_offset, int sub)

//  Analyze the delta-encoded alignment in  delta [0 .. (delta_len - 1)]
//  between  a_part  and  b_part  and store the resulting votes
//  about the a sequence in  Frag [sub] .  The alignment starts
//   a_offset  bytes in from the start of the a sequence in  Frag [sub] .
//   a_len  and  b_len  are the lengths of the prefixes of  a_part  and
//   b_part , resp., that align.

  {
   int  prev_match, next_match;
   Vote_t  vote [MAX_FRAG_LEN];
   int  ct;
   int  i, j, k, m, p;


   if  (a_len < 0 || b_len < 0)
       {
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Negative length:  a_len = %d  b_len = %d  sub = %d\n",
                 a_len, b_len, sub);
        exit (-3);
       }

   vote [0] . frag_sub = -1;
   vote [0] . align_sub = -1;
   vote [0] . vote_val = A_SUBST;   // Dummy value
   ct = 1;
   i = j = p = 0;

   for  (k = 0;  k < delta_len;  k ++)
     {
      for  (m = 1;  m < abs (delta [k]);  m ++)
        {
         if  (a_part [i] != b_part [j])
             {
              vote [ct] . frag_sub = i;
              vote [ct] . align_sub = p;
              switch  (b_part [j])
                {
                 case  'a' :
                   vote [ct] . vote_val = A_SUBST;
                   break;
                 case  'c' :
                   vote [ct] . vote_val = C_SUBST;
                   break;
                 case  'g' :
                   vote [ct] . vote_val = G_SUBST;
                   break;
                 case  't' :
                   vote [ct] . vote_val = T_SUBST;
                   break;
                 default :
                   fprintf (stderr, "ERROR:  [1] Bad sequence char \'%c\' (ASCII %d)\n",
                            b_part [j], (int) b_part [j]);
                   exit (1);
                }
              ct ++;           
             }
         i ++;
         j ++;
         p ++;
        }
      if  (delta [k] < 0)
          {
           vote [ct] . frag_sub = i - 1;
           vote [ct] . align_sub = p;
           switch  (b_part [j])
              {
               case  'a' :
                 vote [ct] . vote_val = A_INSERT;
                 break;
               case  'c' :
                 vote [ct] . vote_val = C_INSERT;
                 break;
               case  'g' :
                 vote [ct] . vote_val = G_INSERT;
                 break;
               case  't' :
                 vote [ct] . vote_val = T_INSERT;
                 break;
               default :
                 fprintf (stderr, "ERROR:  [2] Bad sequence char \'%c\' (ASCII %d)\n",
                          b_part [j], (int) b_part [j]);
                   exit (2);
              }
           ct ++;
           j ++;
           p ++;
          }
        else
          {
           vote [ct] . frag_sub = i;
           vote [ct] . align_sub = p;
           vote [ct] . vote_val = DELETE;
           ct ++;
           i ++;
           p ++;
          }
     }

   while  (i < a_len)
     {
      if  (a_part [i] != b_part [j])
          {
           vote [ct] . frag_sub = i;
           vote [ct] . align_sub = p;
           switch  (b_part [j])
             {
              case  'a' :
                vote [ct] . vote_val = A_SUBST;
                break;
              case  'c' :
                vote [ct] . vote_val = C_SUBST;
                break;
              case  'g' :
                vote [ct] . vote_val = G_SUBST;
                break;
              case  't' :
                vote [ct] . vote_val = T_SUBST;
                break;
              default :
                fprintf (stderr, "ERROR:  [3] Bad sequence char \'%c\' (ASCII %d)\n",
                         b_part [j], (int) b_part [j]);
                fprintf (stderr, "i = %d  a_len = %d  j = %d  b_len = %d\n",
                         i, a_len, j, b_len);
                exit (3);
             }
           ct ++;           
          }
      i ++;
      j ++;
      p ++;
     }

   vote [ct] . frag_sub = i;
   vote [ct] . align_sub = p;

   for  (i = 1;  i <= ct;  i ++)
     {
      int  k, p_lo, p_hi;

      prev_match = vote [i] . align_sub - vote [i - 1] . align_sub - 1;
      p_lo = (i == 1 ? 0 : End_Exclude_Len);
      p_hi = (i == ct ? prev_match : prev_match - End_Exclude_Len);
      if  (prev_match >= Kmer_Len)
          {
           for  (p = 0;  p < p_lo;  p ++)
             Cast_Vote
                 (Matching_Vote (a_part [vote [i - 1] . frag_sub + p + 1]),
                  a_offset + vote [i - 1] . frag_sub + p + 1, sub);

           for  (p = p_lo;  p < p_hi;  p ++)
             {
              k = a_offset + vote [i - 1] . frag_sub + p + 1;
              if  (Frag [sub] . vote [k] . confirmed < MAX_VOTE)
                  Frag [sub] . vote [k] . confirmed ++;
              if  (p < p_hi - 1
                       && Frag [sub] . vote [k] . no_insert < MAX_VOTE)
                  Frag [sub] . vote [k] . no_insert ++;
             }

           for  (p = p_hi;  p < prev_match;  p ++)
             Cast_Vote
                 (Matching_Vote (a_part [vote [i - 1] . frag_sub + p + 1]),
                  a_offset + vote [i - 1] . frag_sub + p + 1, sub);
          }
      if  (i < ct
            && (prev_match > 0
                  || vote [i - 1] . vote_val <= T_SUBST
                  || vote [i] . vote_val <= T_SUBST))
               // Don't allow consecutive inserts
          {
           next_match = vote [i + 1] . align_sub - vote [i] . align_sub - 1;
           if  (prev_match + next_match >= Vote_Qualify_Len)
               Cast_Vote (vote [i] . vote_val, a_offset + vote [i] . frag_sub, sub);
          }
     }

   if  (Verbose_Level > 0)
       {
        int  ct = 0;

        printf (">a_part\n");
        for  (j = 0;  a_part [j] != '\0';  j ++)
          {
           if  (ct == 60)
               {
                putchar ('\n');
                ct = 0;
               }
           if  (ct == 0)
               printf ("   ");
           putchar (Frag [sub] . vote [a_offset + j] . confirmed ? '*' : ' ');
           ct ++;
          }
        putchar ('\n');
       }

   return;
  }



#if USE_NEW_STUFF
static void  Analyze_Diffs
    (void)

//  Analyze the alignments stored in  diff_list  for each entry
//  in  Frag .  Output appropriate ones as overlaps and if
//   Doing_Corrections  is true, also output corrections for
//  each one.

  {
   FILE  * fp;
   int  i;

//**ALD
// Eventually needs to be multi-threaded, probably in the same way that
// original votes were done, i.e., thread p does frag i iff (i % num_threads === p)

   if (Doing_Corrections)
     fp = File_Open (Correction_Filename, "wb");

   fprintf (stderr, "Start analyzing alignments  Num_Frags = %d\n", Num_Frags);
   for (i = 0; i < Num_Frags; i ++)
     Analyze_Frag (fp, i);

   if (Doing_Corrections)
     fclose (fp);

   return;
  }



static void  Analyze_Frag
    (FILE * fp, int sub)

//  Analyze all the potential overlaps to read  sub  contained in
//   Frag [sub] . diff_list  and determine real overlaps and corrections
//  to read  sub

  {
   New_Vote_t  * vote;
   Sequence_Diff_t  * mod_dp;
   char  correct [2 * MAX_FRAG_LEN];  // correction string
   short unsigned  insert_size [MAX_FRAG_LEN];
   char  * mod_seq;
   int  frag_len, mod_len;
   int  i, n;

   if (Frag [sub] . sequence == NULL || Frag [sub] . num_diffs == 0)
     {  // deleted read or read with no overlaps
      if (Doing_Corrections)
        Output_Correction_Header (fp, sub);
      return;
     }

   frag_len = strlen (Frag [sub] . sequence);
   for (i = 0; i < frag_len; i++)
     insert_size [i] = 0;

   n = Frag [sub] . num_diffs;
   for (i = 0; i < n; i ++)
     {
      Set_Insert_Sizes (insert_size, Frag [sub] . diff_list + i);

      if (Verbose_Level > 2)
        {
         printf ("Alignment #%d:\n", i + 1);
         Display_Diffs (Frag [sub] . diff_list + i, Frag [sub] . sequence,
              frag_len);
        }
     }

   if (Verbose_Level > 2)
     {
      printf ("Insert_Lengths:\n");
      for (i = 0; i < frag_len; i ++)
        if (0 < insert_size [i])
          printf ("%4d:  %c %3d\n", i, Frag [sub] . sequence [i], insert_size [i]);
     }

   mod_dp = (Sequence_Diff_t *) safe_calloc (n, sizeof (Sequence_Diff_t));
   for (i = 0; i < n; i ++)
     Modify_For_Inserts (mod_dp + i, Frag [sub] . diff_list + i, insert_size);
   mod_seq = Insert_Gaps (Frag [sub] . sequence, insert_size, frag_len);
   mod_len = strlen (mod_seq);
   vote = (New_Vote_t *) safe_calloc (mod_len, sizeof (New_Vote_t));

   //**ALD Should refine the alignment in here so insertions are internally
   // consistent.  E.g., to prevent:
   //     s1:  ..atcgatgcta
   //     s2:  ..atcgt-gcta
   //    Ref:  ..atcg--gcta

//**ALD
   if (Verbose_Level > 0)
     Display_Multialignment (stdout, sub, mod_seq, NULL, mod_len, mod_dp, n);

//**ALD
if (0)
{
 printf ("\nsub=%d  homopoly=%u\n", sub, Frag [sub] . is_homopoly_type);
 for (i = 0; i < n; i ++)
   printf (" %u", mod_dp [i] . is_homopoly_type);
 putchar ('\n');
}

   Eliminate_Correlated_Diff_Olaps (sub, frag_len, insert_size);

   // Output the overlaps
   for (i = 0; i < n; i ++)
     if (! Frag [sub] . diff_list [i] . disregard)
       Output_Olap_From_Diff (Frag [sub] . diff_list + i, sub);

   for (i = 0; i < n; i ++)
     if (! Frag [sub] . diff_list [i] . disregard)
       {
        if (mod_dp [i] . is_homopoly_type)
//          Set_Homopoly_Votes_From_Diffs (sub, Frag [sub] . diff_list + i);
          Set_New_Homopoly_Votes (vote, mod_seq, mod_len, mod_dp + i);
        else
//          Set_Votes_From_Diffs (sub, Frag [sub] . diff_list + i);
          Set_New_Standard_Votes (vote, mod_len, mod_dp + i);
       }

   if (Frag [sub] . is_homopoly_type)
//     Set_Self_Homopoly_Votes (sub, frag_len);
     Set_New_Self_Homopoly_Votes (vote, mod_seq, mod_len);
   else
     Set_Self_Votes (sub, frag_len);

//**ALD
   if (Verbose_Level > 1)
     {
      printf ("\n>%d\n", Lo_Frag_IID + sub);
      Show_New_Votes (stdout, mod_seq, vote, mod_len);
     }

   if (Verbose_Level > 1)
     Show_Frag_Votes (stdout, sub);

   if (Doing_Corrections)
     {
      if (Frag [sub] . is_homopoly_type)
        Determine_Homopoly_Corrections (fp, sub, vote, mod_seq, correct, mod_len);
      else
        Determine_Standard_Corrections (fp, sub, correct);

//**ALD
      if (Verbose_Level > 0)
        Display_Multialignment (stdout, sub, mod_seq, correct, mod_len, mod_dp, n);
     }


   for (i = 0; i < n; i ++)
     free (mod_dp [i] . de);
   free (mod_dp);
   free (vote);

   return;
  }
#endif



static int  Binomial_Bound
    (int e, double p, int Start, double Limit)

//  Return the smallest  n >= Start  s.t.
//    prob [>= e  errors in  n  binomial trials (p = error prob)]
//          > Limit

  {
   double  Normal_Z, Mu_Power, Factorial, Poisson_Coeff;
   double  q, Sum, P_Power, Q_Power, X;
   int  k, n, Bin_Coeff, Ct;

   q = 1.0 - p;
   if  (Start < e)
       Start = e;

   for  (n = Start;  n < MAX_FRAG_LEN;  n ++)
     {
      if  (n <= 35)
          {
           Sum = 0.0;
           Bin_Coeff = 1;
           Ct = 0;
           P_Power = 1.0;
           Q_Power = pow (q, n);

           for  (k = 0;  k < e && 1.0 - Sum > Limit;  k ++)
             {
              X = Bin_Coeff * P_Power * Q_Power;
              Sum += X;
              Bin_Coeff *= n - Ct;
              Bin_Coeff /= ++ Ct;
              P_Power *= p;
              Q_Power /= q;
             }
           if  (1.0 - Sum > Limit)
               return  n;
          }
        else
          {
           Normal_Z = (e - 0.5 - n * p) / sqrt (n * p * q);
           if  (Normal_Z <= NORMAL_DISTRIB_THOLD)
               return  n;
           Sum = 0.0;
           Mu_Power = 1.0;
           Factorial = 1.0;
           Poisson_Coeff = exp (- n * p);
           for  (k = 0;  k < e;  k ++)
             {
              Sum += Mu_Power * Poisson_Coeff / Factorial;
              Mu_Power *= n * p;
              Factorial *= k + 1;
             }
           if  (1.0 - Sum > Limit)
               return  n;
          }
     }

   return  MAX_FRAG_LEN;
  }



static int  By_A_Lo
  (const void * a, const void * b)

//  Compare the values in  a  and  b  as  (* Sequence_Diff_t) 's,
//  first by  a_lo , then by  a_hi.
//  Return  -1  if  a < b ,  0  if  a == b , and  1  if  a > b .
//  Used for  qsort .

  {
   Sequence_Diff_t  * x, * y;

   x = (Sequence_Diff_t *) a;
   y = (Sequence_Diff_t *) b;

   if  (x -> a_lo < y -> a_lo)
       return  -1;
   else if  (x -> a_lo > y -> a_lo)
       return  1;
   else if  (x -> a_hi < y -> a_hi)
       return  -1;
   else if  (x -> a_hi > y -> a_hi)
       return  1;

   return  0;
  }



static int  By_B_IID
  (const void * a, const void * b)

//  Compare the values in  a  and  b  as  (* Olap_Info_t) 's,
//  first by  b_iid , then by  a_iid.
//  Return  -1  if  a < b ,  0  if  a == b , and  1  if  a > b .
//  Used for  qsort .

  {
   Olap_Info_t  * x, * y;

   x = (Olap_Info_t *) a;
   y = (Olap_Info_t *) b;

   if  (x -> b_iid < y -> b_iid)
       return  -1;
   else if  (x -> b_iid > y -> b_iid)
       return  1;
   else if  (x -> a_iid < y -> a_iid)
       return  -1;
   else if  (x -> a_iid > y -> a_iid)
       return  1;

   return  0;
  }



static void  Cast_Confirmation_Vote
  (Vote_Tally_t * vote)

// Add 1 to the confirmation count in  vote  unless it's already
// at the maximum value

  {
   if (vote -> confirmed < MAX_VOTE)
     vote -> confirmed ++;

   return;
  }



static void  Cast_Delete_Vote
  (Vote_Tally_t * vote)

// Add 1 to the  deletes  count in  vote  unless it's already
// at the maximum value

  {
   if (vote -> deletes < MAX_VOTE)
     vote -> deletes ++;

   return;
  }



static void  Cast_Insert_Vote
  (Vote_Tally_t * vote, unsigned ch)

// Add 1 to the insertion count in  vote  corresponding to character number
//  ch  (0,1,2,3=A,C,G,T resp) unless the count is already  at the maximum value

  {
   switch  (ch)
     {
      case 0 :
        if (vote -> a_insert < MAX_VOTE)
          vote -> a_insert ++;
        break;
      case 1 :
        if (vote -> c_insert < MAX_VOTE)
          vote -> c_insert ++;
        break;
      case 2 :
        if (vote -> g_insert < MAX_VOTE)
          vote -> g_insert ++;
        break;
      case 3 :
        if (vote -> t_insert < MAX_VOTE)
          vote -> t_insert ++;
        break;
      default :
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Bad character value %u not in range 0..3\n", ch);
        exit (EXIT_FAILURE);
     }

   return;
  }



static void  Cast_New_Vote_Char
  (New_Vote_t * vp, char ch)

// Add 1 to the count corresponding to  ch  in  vp .

  {
   switch (tolower (ch))
     {
      case 'a' :
        vp -> a_ct ++;
        break;
      case 'c' :
        vp -> c_ct ++;
        break;
      case 'g' :
        vp -> g_ct ++;
        break;
      case 't' :
        vp -> t_ct ++;
        break;
      case '-' :
        vp -> gap_ct ++;
        break;
      default :
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Bad character '%c'\n", ch);
        exit (EXIT_FAILURE);
     }

   return;
  }



static void  Cast_New_Vote_Code
  (New_Vote_t * vp, unsigned code)

// Add 1 to the count corresponding to the character with code  code  in  vp .

  {
   switch (code)
     {
      case 0 :
        vp -> a_ct ++;
        break;
      case 1 :
        vp -> c_ct ++;
        break;
      case 2 :
        vp -> g_ct ++;
        break;
      case 3 :
        vp -> t_ct ++;
        break;
      default :
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Bad character code '%u\n", code);
        exit (EXIT_FAILURE);
     }

   return;
  }



static void  Cast_No_Insert_Vote
  (Vote_Tally_t * vote)

// Add 1 to the  no_insert  count in  vote  unless it's already
// at the maximum value

  {
   if (vote -> no_insert < MAX_VOTE)
     vote -> no_insert ++;

   return;
  }



static void  Cast_Substitution_Vote
  (Vote_Tally_t * vote, unsigned ch)

// Add 1 to the substitution count in  vote  corresponding to character number
//  ch  (0,1,2,3=A,C,G,T resp) unless the count is already  at the maximum value

  {
   switch  (ch)
     {
      case 0 :
        if (vote -> a_subst < MAX_VOTE)
          vote -> a_subst ++;
        break;
      case 1 :
        if (vote -> c_subst < MAX_VOTE)
          vote -> c_subst ++;
        break;
      case 2 :
        if (vote -> g_subst < MAX_VOTE)
          vote -> g_subst ++;
        break;
      case 3 :
        if (vote -> t_subst < MAX_VOTE)
          vote -> t_subst ++;
        break;
      default :
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Bad character value %u not in range 0..3\n", ch);
        exit (EXIT_FAILURE);
     }

   return;
  }



static void  Cast_Vote
  (Vote_Value_t val, int p, int sub)

//  Add vote  val  to  Frag [sub]  at sequence position  p

  {
   switch  (val)
     {
      case  DELETE :
        if  (Frag [sub] . vote [p] . deletes < MAX_VOTE)
            Frag [sub] . vote [p] . deletes ++;
        break;
      case  A_SUBST :
        if  (Frag [sub] . vote [p] . a_subst < MAX_VOTE)
            Frag [sub] . vote [p] . a_subst ++;
        break;
      case  C_SUBST :
        if  (Frag [sub] . vote [p] . c_subst < MAX_VOTE)
            Frag [sub] . vote [p] . c_subst ++;
        break;
      case  G_SUBST :
        if  (Frag [sub] . vote [p] . g_subst < MAX_VOTE)
            Frag [sub] . vote [p] . g_subst ++;
        break;
      case  T_SUBST :
        if  (Frag [sub] . vote [p] . t_subst < MAX_VOTE)
            Frag [sub] . vote [p] . t_subst ++;
        break;
      case  A_INSERT :
        if  (Frag [sub] . vote [p] . a_insert < MAX_VOTE)
            Frag [sub] . vote [p] . a_insert ++;
        break;
      case  C_INSERT :
        if  (Frag [sub] . vote [p] . c_insert < MAX_VOTE)
            Frag [sub] . vote [p] . c_insert ++;
        break;
      case  G_INSERT :
        if  (Frag [sub] . vote [p] . g_insert < MAX_VOTE)
            Frag [sub] . vote [p] . g_insert ++;
        break;
      case  T_INSERT :
        if  (Frag [sub] . vote [p] . t_insert < MAX_VOTE)
            Frag [sub] . vote [p] . t_insert ++;
        break;
      case  NO_VOTE :
        // do nothing
        break;
      default :
        fprintf (stderr, "ERROR:  Illegal vote type\n");
     }

   return;
  }



static int  Char_Matches
  (char ch, unsigned code)

// Return  TRUE  iff character  ch  matches the character code
// number in  code .

  {
   unsigned  x;
   
   switch (tolower (ch))
     {
      case  'a' :
        x = 0;
        break;
      case  'c' :
        x = 1;
        break;
      case  'g' :
        x = 2;
        break;
      case  't' :
        x = 3;
        break;
      case  '-' :
      case  'x' :
        x = 99;   // impossible value to guarantee a mismatch
        break;
      default :
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Bad character %c not A,C,G,T,X,-\n", ch);
        exit (EXIT_FAILURE);
     }

   return (x == code);
  }



static char  Complement
    (char ch)

/*  Return the DNA complement of  ch . */

  {
   switch  (tolower ((int) ch))
     {
      case  'a' :
        return  't';
      case  'c' :
        return  'g';
      case  'g' :
        return  'c';
      case  't' :
        return  'a';
      case  'n' :
        return  'n';
      default :
        fprintf (stderr, "ERROR(complement):  Unexpected character `%c\'\n", ch);
        exit (-1);
     }

   return  'x';    // Just to make the compiler happy
  }



static void  Compute_Delta
    (int delta [], int * delta_len, int * edit_array [MAX_ERRORS],
     int e, int d, int row)

//  Set  delta  to the entries indicating the insertions/deletions
//  in the alignment encoded in  edit_array  ending at position
//  edit_array [e] [d] .   row  is the position in the first
//  string where the alignment ended.  Set  (* delta_len)  to
//  the number of entries in  delta .

  {
   int  delta_stack [MAX_ERRORS];
   int  from, last, max;
   int  i, j, k;

   last = row;
   (* delta_len) = 0;

   for  (k = e;  k > 0;  k --)
     {
      from = d;
      max = 1 + edit_array [k - 1] [d];
      if  ((j = edit_array [k - 1] [d - 1]) > max)
          {
           from = d - 1;
           max = j;
          }
      if  ((j = 1 + edit_array [k - 1] [d + 1]) > max)
          {
           from = d + 1;
           max = j;
          }
      if  (from == d - 1)
          {
           delta_stack [(* delta_len) ++] = max - last - 1;
           d --;
           last = edit_array [k - 1] [from];
          }
      else if  (from == d + 1)
          {
           delta_stack [(* delta_len) ++] = last - (max - 1);
           d ++;
           last = edit_array [k - 1] [from];
          }
     }
   delta_stack [(* delta_len) ++] = last + 1;

   k = 0;
   for  (i = (* delta_len) - 1;  i > 0;  i --)
     delta [k ++]
         = abs (delta_stack [i]) * Sign (delta_stack [i - 1]);
   (* delta_len) --;

   return;
  }



static void  Convert_Delta_To_Diff
  (int delta [], int delta_len, char * a_part, char * b_part,
   int  a_len, int b_len, Sequence_Diff_t * diff, int errors,
   Thread_Work_Area_t * wa)

//  Convert the delta-encoded alignment in  delta [0 .. (delta_len - 1)]
//  between  a_part  and  b_part  to a description of the differences
//  between the sequences and store the result in  diff .
//  Allocate new memory for the difference list.
//   a_len  and  b_len  are the lengths of the prefixes of  a_part  and
//   b_part , resp., that align.   errors  is the number of errors in
//  the alignment in  delta .   wa  has work areas in case of
//  multi-threading.
//  Note:  The delta matched A, the reference read to be analyzed/corrected,
//  to B, the other read.  The diff produced here describes how B
//  matches to A.  Substitutions in diff are B characters substituted for
//  A characters; inserts are B characters that should be inserted in A;
//  and deletes are characters that should be deleted from A.

  {
   Diff_Entry_t  diff_list [MAX_ERRORS];
   int  ct;
   int  i, j, k, m, p;

   if (a_len < 0 || b_len < 0)
     {
      fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
      fprintf (stderr, "Negative length:  a_len = %d  b_len = %d\n",
           a_len, b_len);
      exit (-3);
     }

   i = j = ct = 0;

   for (k = 0; k < delta_len; k ++)
     {
      p = 0;
      for (m = 1; m < abs (delta [k]); m ++)
        {
         if (a_part [i] != b_part [j])
           {
            diff_list [ct] . len = p;     // length of exact match region
            diff_list [ct] . action = 2;  // substitution
            switch  (b_part [j])
              {
               case  'a' :
                 diff_list [ct] . ch = 0;
                 break;
               case  'c' :
                 diff_list [ct] . ch = 1;
                 break;
               case  'g' :
                 diff_list [ct] . ch = 2;
                 break;
               case  't' :
                 diff_list [ct] . ch = 3;
                 break;
               default :
                 fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
                 fprintf (stderr, "Bad sequence char \'%c\' (ASCII %d)\n",
                      b_part [i], (int) b_part [i]);
                 exit (EXIT_FAILURE);
              }
            ct ++;
            p = 0;
           }
         else
           p ++;
         i ++;
         j ++;
        }
      if (delta [k] > 0)
        {
         diff_list [ct] . len = p;     // length of exact match region
         diff_list [ct] . action = 1;  // delete
         diff_list [ct] . ch = 0;      // doesn't matter
         ct ++;
         i ++;
        }
      else
        {
         diff_list [ct] . len = p;     // length of exact match region
         diff_list [ct] . action = 0;  // insert
         switch  (b_part [j])
           {
            case  'a' :
              diff_list [ct] . ch = 0;
              break;
            case  'c' :
              diff_list [ct] . ch = 1;
              break;
            case  'g' :
              diff_list [ct] . ch = 2;
              break;
            case  't' :
              diff_list [ct] . ch = 3;
              break;
            default :
              fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
              fprintf (stderr, "Bad sequence char \'%c\' (ASCII %d)\n",
                   b_part [i], (int) b_part [i]);
              exit (EXIT_FAILURE);
           }
         ct ++;
         j ++;
        }
     }

   for (p = 0; i < a_len; i ++, j ++)
     {
      if (a_part [i] != b_part [j])
        {
         diff_list [ct] . len = p;     // length of exact match region
         diff_list [ct] . action = 2;  // substitution
         switch  (b_part [j])
           {
            case  'a' :
              diff_list [ct] . ch = 0;
              break;
            case  'c' :
              diff_list [ct] . ch = 1;
              break;
            case  'g' :
              diff_list [ct] . ch = 2;
              break;
            case  't' :
              diff_list [ct] . ch = 3;
              break;
            default :
              fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
              fprintf (stderr, "Bad sequence char \'%c\' (ASCII %d)\n",
                   b_part [i], (int) b_part [i]);
              exit (EXIT_FAILURE);
           }
         ct ++;
         p = 0;
        }
      else
        p ++;
     }

   if (0 < p)
     {
      diff_list [ct] . len = p;     // length of exact match region
      diff_list [ct] . action = 3;  // noop
      diff_list [ct] . ch = 0;      // doesn't really matter
      ct ++;
     }

   diff -> diff_len = ct;
   diff -> de = (Diff_Entry_t *) safe_malloc (ct * sizeof (Diff_Entry_t));
   for (i = 0; i < ct; i ++)
     diff -> de [i] = diff_list [i];

   return;
  }



static void  Determine_Homopoly_Corrections
    (FILE * fp, int sub, New_Vote_t * vote, char * seq,
     char * correct, int len)

// Determine the corrections for  Frag [sub]  based on the votes in
//  vote  corresponding to the sequence in  seq , both of length  len .
// Put the corrected string in  correct .  Output corrections to  fp .

  {
   Correction_Output_t  out;
   char  ch, hp_ch = 'x';
   int  hp_width = 0;   // number of consecutive positions in seq for
                        // the current homopolymer
   int  hp_len, new_hp_len, ch_ct, diff, tot;
   int  hp_ct = 0, hp_sum = 0;
   int  i, j, k;

   for (i = 0; i < len; i ++)
     {
      correct [i] = ' ';
      ch = Homopoly_Should_Be (seq [i], vote + i, & ch_ct, & tot);
      if (ch != hp_ch && ch != '-')
        {  // end of prior homopoly run--adjust length if necessary
         if (i > 0)
           {
            new_hp_len = rint (EPSILON + (1.0 * hp_sum) / hp_ct);
            diff = new_hp_len - hp_len;
//**ALD
if (0)
{
 double  x = (1.0 * hp_sum) / hp_ct;
 printf ("%3d:  %c  hp_ct=%d  hp_sum=%d  hp_len=%d  diff=%d  hp_width=%d  x=%.3f\n",
      j, seq [j], hp_ct, hp_sum, hp_len, diff, hp_width, x);
}
            if (diff < 0 && hp_width > 1 && (new_hp_len > 0 || vote [j] . mutable))
              {  // delete (- diff) copies of hp_ch preceding here
//**ALD
//printf (" .. delete %d copies of %c in %d..%d\n", - diff, hp_ch, j, i - 1);
               for (k = i - 1; k >= j && diff < 0; k --)
                 if (seq [k] == hp_ch && correct [k] == ' ')
                   {
                    correct [k] = '-';
                    diff ++;
                   }
              }
            else if (diff > 0 && hp_width > 1)
              {  // insert diff copies of hp_ch after j
//**ALD
//printf (" .. insert %d copies of %c int %d..%d\n", diff, hp_ch, j, i - 1);
               for (k = j; k < i && diff > 0; k ++)
                 if (seq [k] == '-' && correct [k] == ' ')
                   {
                    correct [k] = hp_ch;
                    diff --;
                   }
              }
           }
         // new homopoly run starts here
         hp_len = hp_width = 0;
         hp_ch = ch;
         hp_ct = hp_sum = 0;
         j = i;
        }
      hp_sum += Votes_For (hp_ch, vote + i);
//**ALD
//printf ("... i=%d  %c  hp_ch=%c  votes_for=%d  hp_sum=%d\n", i, seq [i], hp_ch,
//     Votes_For (hp_ch, vote + i), hp_sum);
      if (hp_ct < tot)   // use maximum coverage depth
        hp_ct = tot;
      if (seq [i] == hp_ch)
        hp_len ++;
      if (seq [i] == hp_ch || seq [i] == '-')
        hp_width ++;

      if (ch != seq [i])
        {
         correct [i] = ch;
         if (ch == hp_ch)
           hp_len ++;
         else if (seq [i] == hp_ch)
           hp_len --;   // decrease homopoly count so don't re-delete later
        }
     }

//**ALD
   if (Verbose_Level > 1)
     Show_Corrections (stdout, seq, correct, len);

   // Output corrections
   Output_Correction_Header (fp, sub);
   for (i = j = 0; i < len; i ++)
     {
      if (seq [i] != '-')
        j ++;
      if (correct [i] != ' ')
        {
         assert (correct [i] != seq [i]);
         out . corr . is_ID = FALSE;
         out . corr . pos = j - 1;
         if (correct [i] == '-')
           out . corr . type = DELETE;
         else if (seq [i] == '-')
           {
            switch (correct [i])
              {
               case 'a' :
                 out . corr . type = A_INSERT;
                 break;
               case 'c' :
                 out . corr . type = C_INSERT;
                 break;
               case 'g' :
                 out . corr . type = G_INSERT;
                 break;
               case 't' :
                 out . corr . type = T_INSERT;
                 break;
              }
           }
         else
           {
            switch (correct [i])
              {
               case 'a' :
                 out . corr . type = A_SUBST;
                 break;
               case 'c' :
                 out . corr . type = C_SUBST;
                 break;
               case 'g' :
                 out . corr . type = G_SUBST;
                 break;
               case 't' :
                 out . corr . type = T_SUBST;
                 break;
              }
           }
         fwrite (& out, sizeof (Correction_Output_t), 1, fp);
        }
     }

   return;
  }



static void  Determine_Standard_Corrections
    (FILE * fp, int sub, char * correct)

// Determine the corrections for  Frag [sub]  based on its votes
// and output the corrections to  fp .  Assume  Frag [sub]  is
// a normal read with trusted homopolymer run lengths.
// Put the corrected sequence into  correct .

  {
   int  i, j;

   //**ALD  Not done yet

   return;
  }



#define  DISPLAY_WIDTH   60

static void  Display_Alignment
    (char * a, int a_len, char * b, int b_len, int delta [], int delta_ct,
     int capitalize_start)

//  Show (to  stdout ) the alignment encoded in  delta [0 .. (delta_ct - 1)]
//  between strings  a [0 .. (a_len - 1)]  and  b [0 .. (b_len - 1)] .
//  Capitialize  a  characters for positions at and after  capitalize_start .

  {
   int  i, j, k, m, top_len, bottom_len;
   char  top [2000], bottom [2000];

   i = j = top_len = bottom_len = 0;
   for  (k = 0;  k < delta_ct;  k ++)
     {
      for  (m = 1;  m < abs (delta [k]);  m ++)
        {
         if  (i >= capitalize_start)
             top [top_len ++] = toupper (a [i ++]);
           else
             top [top_len ++] = a [i ++];
         j ++;
        }
      if  (delta [k] < 0)
          {
           top [top_len ++] = '-';
           j ++;
          }
        else
          {
           if  (i >= capitalize_start)
               top [top_len ++] = toupper (a [i ++]);
             else
               top [top_len ++] = a [i ++];
          }
     }
   while  (i < a_len && j < b_len)
     {
      if  (i >= capitalize_start)
          top [top_len ++] = toupper (a [i ++]);
        else
          top [top_len ++] = a [i ++];
      j ++;
     }
   top [top_len] = '\0';
     

   i = j = 0;
   for  (k = 0;  k < delta_ct;  k ++)
     {
      for  (m = 1;  m < abs (delta [k]);  m ++)
        {
         bottom [bottom_len ++] = b [j ++];
         i ++;
        }
      if  (delta [k] > 0)
          {
           bottom [bottom_len ++] = '-';
           i ++;
          }
        else
          {
           bottom [bottom_len ++] = b [j ++];
          }
     }
   while  (j < b_len && i < a_len)
     {
      bottom [bottom_len ++] = b [j ++];
      i ++;
     }
   bottom [bottom_len] = '\0';


   for  (i = 0;  i < top_len || i < bottom_len;  i += DISPLAY_WIDTH)
     {
      putchar ('\n');
      printf ("A: ");
      for  (j = 0;  j < DISPLAY_WIDTH && i + j < top_len;  j ++)
        putchar (top [i + j]);
      putchar ('\n');
      printf ("B: ");
      for  (j = 0;  j < DISPLAY_WIDTH && i + j < bottom_len;  j ++)
        putchar (bottom [i + j]);
      putchar ('\n');
      printf ("   ");
      for  (j = 0;  j < DISPLAY_WIDTH && i + j < bottom_len && i + j < top_len;
                j ++)
        if  (top [i + j] != ' ' && bottom [i + j] != ' '
                 && tolower (top [i + j]) != tolower (bottom [i + j]))
            putchar ('^');
          else
            putchar (' ');
      putchar ('\n');
     }

   return;
  }



static void  Display_Diffs
  (const Sequence_Diff_t * dp, const char * a, int a_len)

//  Show (to  stdout ) the differences encoded in  dp
//  to string  a [0 .. (a_len - 1)] .

  {
   char  alpha [5] = "acgt";
   char  top [2000], bottom [2000];
   int  i, j, k, m, top_len, bottom_len;

   j = dp -> a_lo;
   top_len = bottom_len = 0;
   for (k = 0; k < dp -> diff_len; k ++)
     {
      for (m = 0; m < dp -> de [k] . len; m ++)
        {
         top [top_len ++] = '.';
         bottom [bottom_len ++] = a [j ++];
        }
      switch (dp -> de [k] . action)
        {
         case 0 :    // insert
           top [top_len ++] = alpha [dp -> de [k] . ch];
           bottom [bottom_len ++] = '-';
           break;
         case 1 :    // delete
           top [top_len ++] = '-';
           bottom [bottom_len ++] = a [j ++];
           break;
         case 2 :    // substitute
           top [top_len ++] = alpha [dp -> de [k] . ch];
           bottom [bottom_len ++] = a [j ++];
           break;
         case 3 :    // noop
           // do nothing--should be end of alignment
           break;
        }
     }
//**ALD
#if 0
printf ("j = %d  dp -> a_hi = %d  %s\n", j, dp -> a_hi,
   (j == dp -> a_hi ? "" : "whooops"));
#endif

   for (i = 0; i < top_len || i < bottom_len; i += DISPLAY_WIDTH)
     {
      putchar ('\n');
      printf ("A: ");
      for  (j = 0;  j < DISPLAY_WIDTH && i + j < top_len;  j ++)
        putchar (top [i + j]);
      putchar ('\n');
      printf ("B: ");
      for  (j = 0;  j < DISPLAY_WIDTH && i + j < bottom_len;  j ++)
        putchar (bottom [i + j]);
      putchar ('\n');
      printf ("   ");
      for (j = 0; j < DISPLAY_WIDTH && i + j < bottom_len && i + j < top_len; j ++)
        if (top [i + j] != '.'
             && tolower (top [i + j]) != tolower (bottom [i + j]))
          putchar ('^');
        else
          putchar (' ');
      putchar ('\n');
     }

   return;
  }



static void  Display_Frags
  (void)

//  List selected fragments in fasta format to stdout

  {
   int  i;

   for  (i = 0;  i < Num_Frags;  i ++)
     {
      if (Frag [i] . sequence != NULL)
        {
          int  j, ct;

          printf (">%d\n", Lo_Frag_IID + i);
          ct = 0;
          for  (j = 0;  Frag [i] . sequence [j] != '\0';  j ++)
            {
              if  (ct == 60)
                {
                  putchar ('\n');
                  ct = 0;
                }
              putchar (Frag [i] . sequence [j]);
              ct ++;
            }
          putchar ('\n');
        }
     }

   return;
  }



static void  Display_Multialignment
  (FILE * fp, int sub, const char * ref, const char * anno, int ref_len,
   const Sequence_Diff_t * dp, int dp_ct)

// Display to  fp  the multialignment for read  sub  with gap-inserted sequence
// of  ref  (having length  ref_len ) where the aligned sequences
// are in  dp  which has  dp_len  entries.  If  anno  is not null
// display it underneath  ref .

  {
   Sequence_Diff_t  * mod_dp;
   char  * mod_seq;
   int  ct, lo, hi, mod_len;
   int  i, j;

   fprintf (fp, "\nMultialignment for read %d:\n", sub);

   mod_dp = (Sequence_Diff_t *) safe_calloc (dp_ct, sizeof (Sequence_Diff_t));
   for (i = 0; i < dp_ct; i ++)
     mod_dp [i] = dp [i];

   qsort (mod_dp, dp_ct, sizeof (Sequence_Diff_t), By_A_Lo);


//**ALD
#if 0
for (i = 0; i < dp_ct; i ++)
  {
   printf ("dp [%d]:\n", i);
   for (j = 0; j < mod_dp [i] . diff_len; j ++)
     {
      Sequence_Diff_t  * dp = Frag [sub] . diff_list + i;

      printf ("%3d: %5u %2u %2u", j, mod_dp [i] . de [j] . len,
         mod_dp [i] . de [j] . action, mod_dp [i] . de [j] . ch);
      if (j < dp -> diff_len)
        printf ("   %5u %2u %2u", dp -> de [j] . len,
              dp -> de [j] . action, dp -> de [j] . ch);
      putchar ('\n');
     }

   printf ("\nAlignment [%d]:\n", i);
   Display_Diffs (mod_dp + i, ref, ref_len);
  }
#endif

   for (lo = 0; lo < ref_len; lo += DISPLAY_WIDTH)
     {
      fputc ('\n', fp);
      hi = lo + DISPLAY_WIDTH;
      if (ref_len < hi)
        hi = ref_len;
      for (i = 0; i < dp_ct; i ++)
        {
         fprintf (fp, "%7d:  ", mod_dp [i] . b_iid);
         Display_Partial_Diff (fp, mod_dp + i, lo, hi, ref);
        }

      fputc ('\n', fp);
      fprintf (fp, "%7s:  ", "ref");
      for (j = lo; j < hi; j ++)
        fputc (ref [j], fp);
      fputc ('\n', fp);
      if (anno != NULL)
        {
         fprintf (fp, "%7s:  ", "");
         for (j = lo; j < hi; j ++)
           fputc (anno [j], fp);
         fputc ('\n', fp);
        }
     }

   free (mod_dp);

   return;
  }



static void  Display_Partial_Diff
  (FILE * fp, const Sequence_Diff_t * dp, int lo, int hi, const char * a)

//  Display to  fp  the differences encoded in  dp  but only to the
//  subrange of sequence  a  between positions  lo  and  hi .

  {
   char  alpha [5] = "acgt";
   int  j, k, m, p;

   // see if entire region to print misses the alignment region
   if (hi <= dp -> a_lo || dp -> a_hi <= lo)
     {
      for (j = lo; j < hi; j ++)
        fputc (' ', fp);
      fprintf (fp, " <\n");  // so can see the end of the line
      return;
     }

   // print spaces for the part of the region that precedes the start
   // of the alignment if any 
   for (j = lo; j < dp -> a_lo && j < hi; j ++)
     fputc (' ', fp);

   // run through the alignment and print the positions that
   // lie between  lo  and  hi
   p = dp -> a_lo;
   for (k = 0; k < dp -> diff_len && p < hi; k ++)
     {
      for (m = 0; m < dp -> de [k] . len && p < hi; m ++)
        {
         if (lo <= p && p < hi)
           fputc (a [p], fp);
         p ++;
        }

      if (p < hi)
        switch (dp -> de [k] . action)
          {
           case 0 :    // insert
             if (lo <= p && p < hi - 1)   // no insert at end of alignment
               fputc (alpha [dp -> de [k] . ch], fp);
             break;
           case 1 :    // delete
             if (lo <= p)
               fputc ('-', fp);
             p ++;
             break;
           case 2 :    // substitute
             if (lo <= p)
               fputc (alpha [dp -> de [k] . ch], fp);
             p ++;
             break;
           case 3 :    // noop
             // do nothing--should be end of alignment
             break;
          }
     }

   // print spaces for the part of the region that follows the alignment,
   // if any
   for ( ; p < hi; p ++)
     fputc (' ', fp);

   fprintf (fp, " <\n");  // so can see the end of the line

   return;
  }



static void  Eliminate_Correlated_Diff_Olaps
  (int sub, int frag_len, const short insert_size [])

// Set the  disregard  flag true for entries in  Frag [sub] . diff_list
// that have sufficiently many confirmed differences to the reference
// fragment.   frag_len  is the length of the reference fragment and
//  insert_size  [i]  is the number of insertions each position in
// the reference needed to make a multialignment.

  {
   //**ALD To be filled in
   return;
  }



static void  Extract_Needed_Frags
    (GateKeeperStore *store, int32 lo_frag, int32 hi_frag,
     Frag_List_t * list, int * next_olap)

//  Read fragments  lo_frag .. hi_frag  from  store  and save
//  the ids and sequences of those with overlaps to fragments in
//  global  Frag .

  {

#ifdef USE_STREAM_FOR_EXTRACT
   FragStream  *frag_stream;
   int i;
#endif
   static fragRecord  * frag_read = NULL;
   uint32  frag_iid;
   int  bytes_used, total_len, new_total;
   int  extract_ct, stream_ct;
   int  j;

   if  (frag_read == NULL)
       frag_read = new_fragRecord ();

#ifdef USE_STREAM_FOR_EXTRACT
   frag_stream = openFragStream (store);
   resetFragStream (frag_stream, lo_frag, hi_frag);
#endif

   list -> ct = 0;
   total_len = 0;
   extract_ct = stream_ct = 0;

#ifdef USE_STREAM_FOR_EXTRACT
   for (i = 0; nextFragStream (frag_stream, frag_read, FRAG_S_SEQ)
        && (* next_olap) < Num_Olaps; i ++)
#else
   frag_iid = Olap [(* next_olap)] . b_iid;
   while  ((* next_olap) < Num_Olaps && frag_iid <= hi_frag)
#endif
     {
      char  * seq_ptr;
      char  seq_buff [AS_READ_MAX_LEN + 1];
      unsigned  clear_start, clear_end;
      int  raw_len, result, shredded;

      stream_ct ++;

#ifdef USE_STREAM_FOR_EXTRACT
      getReadIndex_ReadStruct (frag_read, & frag_iid);
      if  (frag_iid < Olap [(* next_olap)] . b_iid)
          continue;
#else
      getFrag (store, frag_iid, frag_read, FRAG_S_SEQ);
#endif

      if (getFragRecordIsDeleted (frag_read))
          goto  Advance_Next_Olap;

      shredded = FALSE;
        // Used in Process_Olap to ignore overlaps between two "shredded" reads
        // Perhaps should check for external reads now

      clear_start = getFragRecordClearRegionBegin (frag_read, AS_READ_CLEAR_OBT);
      clear_end = getFragRecordClearRegionEnd (frag_read, AS_READ_CLEAR_OBT);
      raw_len = getFragRecordSequenceLength (frag_read);
      seq_ptr = getFragRecordSequence (frag_read);

      if (AS_READ_MAX_LEN < clear_end - clear_start)
        {
         fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
         fprintf (stderr, "Read %u is too long:  %d bp; max is %d\n",
              frag_iid, clear_end - clear_start, AS_READ_MAX_LEN);
         exit (-1);
        }

      // Make sure that we have a valid lowercase sequence string
      for (j = clear_start; j < clear_end; j ++)
        seq_buff [j - clear_start] = Filter (seq_ptr [j]);
      seq_buff [clear_end - clear_start] = '\0';

      if  (list -> ct >= list -> size)
          {
           list -> size *= 2;
           assert (list -> size > list -> ct);
           list -> entry = (Frag_List_Entry_t *) safe_realloc
                             (list -> entry, list -> size * sizeof (Frag_List_Entry_t));
          }

      list -> entry [list -> ct] . id = frag_iid;
      list -> entry [list -> ct] . shredded = shredded;
      list -> entry [list -> ct] . is_homopoly_type
           = Is_Homopoly_Type (frag_read, store);
      list -> entry [list -> ct] . trim_5p = clear_start;
      list -> entry [list -> ct] . trim_3p = raw_len - clear_end;
      bytes_used = 1 + clear_end - clear_start;
      new_total = total_len + bytes_used;
      if  (new_total > list -> buffer_size)
          {
           list -> buffer_size *= 2;
           assert (list -> buffer_size >= new_total);
           list -> buffer = (char *) safe_realloc
                               (list -> buffer, list -> buffer_size);
          }
      list -> entry [list -> ct] . start = total_len;
      strcpy (list -> buffer + total_len, seq_buff);
      list -> ct ++;
      total_len = new_total;

      extract_ct ++;

   Advance_Next_Olap:
      while  ((* next_olap) < Num_Olaps
                && Olap [(* next_olap)] . b_iid == frag_iid)
        (* next_olap) ++;
      frag_iid = Olap [(* next_olap)] . b_iid;
     }

#ifdef USE_STREAM_FOR_EXTRACT
   closeFragStream (frag_stream);
#endif

   if  (list -> ct == list -> size)
     {
      list -> size ++;
      list -> entry = (Frag_List_Entry_t *) safe_realloc
                        (list -> entry, list -> size * sizeof (Frag_List_Entry_t));
     }
   list -> entry [list -> ct] . start = total_len;

   fprintf (stderr, "Extracted %d of %d fragments in iid range %d .. %d\n",
            extract_ct, stream_ct, lo_frag, hi_frag);

   return;
  }



static char  Filter
    (char ch)

//  Convert  ch  to lowercase if necessary and if not 'a', 'c', 'g' or 't'
//  make it an 'a'.

  {
   ch = tolower (ch);

   switch  (ch)
     {
      case  'a' :
      case  'c' :
      case  'g' :
      case  't' :
        return  ch;
     }

   return  'a';
  }



static void  Get_Seeds_From_Store
    (char * path, int32 lo_id, int32 hi_id, Olap_Info_t * * olap, int * num)

//  Open overlap store  path  and read from it the overlaps for fragments
//   lo_id .. hi_id , putting them in  (* olap)  for which space
//  is dynamically allocated.  Set  (* num)  to the number of entries
//  in  (* olap) .

  {
   OverlapStore  * ovs = NULL;
   OVSoverlap  ovl;
   uint64  num_olaps = 0;
   uint64  num_read = 0;

   if (lo_id < 1 || hi_id < lo_id)
     {
      fprintf (stderr, "ERROR:  Read iid range is backwards:  lo = %d  hi = %d\n",
           lo_id, hi_id);
      exit (-1);
     }

    ovs = AS_OVS_openOverlapStore (path);

    AS_OVS_setRangeOverlapStore (ovs, lo_id, hi_id);

    num_olaps = AS_OVS_numOverlapsInRange (ovs);

    * olap = (Olap_Info_t *) safe_realloc (* olap, num_olaps * sizeof (Olap_Info_t));
    * num  = 0;

    while (AS_OVS_readOverlapFromStore (ovs, & ovl, AS_OVS_TYPE_MER))
      {
       (* olap) [num_read] . a_iid = ovl . a_iid;
       (* olap) [num_read] . b_iid = ovl . b_iid;
       (* olap) [num_read] . a_hang = ovl . dat . mer . a_pos;
       (* olap) [num_read] . b_hang = ovl . dat . mer . b_pos;
       (* olap) [num_read] . orient = (ovl . dat . mer . fwd ? NORMAL : INNIE);
       (* olap) [num_read] . k_count = ovl . dat . mer . k_count;
       num_read ++;

       if (Verbose_Level > 1)
         {
          printf ("olap %7u %7u %2u %c %c %5u %5u %4u %4u %2u\n",
               ovl . a_iid, ovl . b_iid,
               ovl . dat . mer . compression_length,
               (ovl . dat . mer . fwd ? 'f' : 'r'),
               (ovl . dat . mer . palindrome ? 'p' : '-'),
               ovl . dat . mer . a_pos,
               ovl . dat . mer . b_pos,
               ovl . dat . mer . k_count,
               ovl . dat . mer . k_len,
               ovl . dat . mer . type);
         }
      }

    (* num) = num_read;

   return;
  }



static char  Homopoly_Should_Be
  (char curr, New_Vote_t * vp, int * ch_ct, int * tot)

// Return the character that should be at this column of
// a multialignment with counts in  vp  and the current
// character in the reference string being  curr .
// Set  ch_ct  to the number of votes for the returned character,
// and set  tot  to the total number of votes

  {
   char  mx_ch;
   int  curr_ct, mx_ct;

   mx_ch = 'a';
   mx_ct = vp -> a_ct;
   if (mx_ct < vp -> c_ct)
     {
      mx_ch = 'c';
      mx_ct = vp -> c_ct;
     }
   if (mx_ct < vp -> g_ct)
     {
      mx_ch = 'g';
      mx_ct = vp -> g_ct;
     }
   if (mx_ct < vp -> t_ct)
     {
      mx_ch = 't';
      mx_ct = vp -> t_ct;
     }
   if (mx_ct < vp -> gap_ct)
     {
      mx_ch = '-';
      mx_ct = vp -> gap_ct;
     }

   * tot = vp -> a_ct + vp -> c_ct + vp -> g_ct + vp -> t_ct + vp -> gap_ct;
   * ch_ct = mx_ct;

   if (mx_ch == curr)
     {
      vp -> mutable = FALSE;
      return  mx_ch;   // no change
     }

   switch (tolower (curr))
     {
      case 'a' :
        curr_ct = vp -> a_ct;
        break;
      case 'c' :
        curr_ct = vp -> c_ct;
        break;
      case 'g' :
        curr_ct = vp -> g_ct;
        break;
      case 't' :
        curr_ct = vp -> t_ct;
        break;
     }

   if ((mx_ct > 1 && * tot - mx_ct <= 1)
         || (* tot - mx_ct <= 2 && mx_ct >= 0.8 * (* tot))
         || (curr_ct == 1 && mx_ct > 1)
         || (curr_ct == 2 && mx_ct >= 0.9 * (* tot)))
     {
      vp -> mutable = TRUE;
      return  mx_ch;
     }

   vp -> mutable = FALSE;
   return  curr;
  }



static void  Init_Frag_List
    (Frag_List_t * list)

//  Initilize the entries in fragment list  (* list)

 {
  list -> ct = 0;
  list -> size = 1000;
  list -> entry = (Frag_List_Entry_t *) safe_malloc
                        (Frag_List . size * sizeof (Frag_List_Entry_t));
  list -> buffer_size = Frag_List . size * 550;
  list -> buffer = (char *) safe_malloc (Frag_List . buffer_size);

  return;
 }



static void  Initialize_Globals
    (void)

//  Initialize global variables used in this program

  {
   int  i, offset, del;
   int  e, start;

   if (OVL_Output_Path == NULL)
     {
      fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
      fprintf (stderr, "No overlap output path specified\n");
      exit (EXIT_FAILURE);
     }
   switch (OVL_Output_Type)
     {
      case TEXT_FILE :
        OVL_Output_fp = File_Open (OVL_Output_Path, "w");
        break;
      case BINARY_FILE :
        Binary_OVL_Output_fp = AS_OVS_createBinaryOverlapFile (OVL_Output_Path, FALSE);
        break;
      case OVL_STORE :
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Directly outputting overlaps to binary store not permitted\n");
        exit (EXIT_FAILURE);
     }

   offset = 2;
   del = 6;
   for  (i = 0;  i < MAX_ERRORS;  i ++)
     {
       Edit_Array [i] = Edit_Space + offset;
       offset += del;
       del += 2;
     }

   Char_Match_Value = Error_Rate;

   for  (i = 0;  i <= ERRORS_FOR_FREE;  i ++)
     Edit_Match_Limit [i] = 0;

   start = 1;
   for  (e = ERRORS_FOR_FREE + 1;  e < MAX_ERRORS;  e ++)
     {
      start = Binomial_Bound (e - ERRORS_FOR_FREE, Error_Rate,
                  start, EDIT_DIST_PROB_BOUND);
      Edit_Match_Limit [e] = start - 1;
      assert (Edit_Match_Limit [e] >= Edit_Match_Limit [e - 1]);
     }

   for  (i = 0;  i <= MAX_FRAG_LEN;  i ++)
     Error_Bound [i] = (int) (i * Error_Rate);

   Frag_List . ct = 0;
   Frag_List . size = 1000;
   Frag_List . entry = (Frag_List_Entry_t *) safe_malloc
                         (Frag_List . size * sizeof (Frag_List_Entry_t));
   Frag_List . buffer_size = Frag_List . size * 550;
   Frag_List . buffer = (char *) safe_malloc (Frag_List . buffer_size);

   return;
  }



static void  Init_Thread_Work_Area
    (Thread_Work_Area_t * wa, int id)

//  Initialize variables in work area  (* wa)  used by thread
//  number  i .

  {
   int  del, offset;
   int  i;

   wa -> thread_id = id;
   wa -> failed_olaps = 0;
   wa -> frag_read = new_fragRecord ();
   strcpy (wa -> rev_seq, "acgt");

   offset = 2;
   del = 6;
   for  (i = 0;  i < MAX_ERRORS;  i ++)
     {
      wa -> edit_array [i] = wa -> edit_space + offset;
      offset += del;
      del += 2;
     }

   return;
  }



static char *  Insert_Gaps
  (char * seq, const short insert_size [], int seq_len)

//  Return a (newly allocated) string that is the same as  seq
//  but with  insert_size [i]  gaps ('-'s) inserted after
//  position  i .   seq_len  is the length of  seq .

  {
   char  * s;
   int  i, j, p, sum;

   sum = 0;
   for (i = 0; i < seq_len; i ++)
     sum += insert_size [i];

   s = (char *) safe_malloc (1 + seq_len + sum);

   p = 0;
   for (i = 0; i < seq_len; i ++)
     {
      s [p ++] = seq [i];
      for (j = 0; j < insert_size [i]; j ++)
        s [p ++] = '-';
     }
   s [p] = '\0';

   return  s;
  }



static int  Is_Homopoly_Type
  (fragRecord * fr, GateKeeperStore * gkp)

// Return  TRUE  iff fr is from a library with untrusted homopolymer runs

  {
   GateKeeperLibraryRecord  * libp;
   
   libp = getGateKeeperLibrary (gkp, getFragRecordLibraryIID (fr));

   return (libp -> doNotTrustHomopolymerRuns);
  }



static Vote_Value_t  Matching_Vote
  (char ch)

// Return the substitution vote corresponding to  Ch .

  {
   switch  (tolower (ch))
     {
      case  'a' :
        return  A_SUBST;
      case  'c' :
        return  C_SUBST;
      case  'g' :
        return  G_SUBST;
      case  't' :
        return  T_SUBST;
      default :
        return  NO_VOTE;
     }
  }



static void  Modify_For_Inserts
  (Sequence_Diff_t * mod_dp, Sequence_Diff_t * dp, const short insert_size [])

// Make  mod_dp  be a modified version of  dp  that allows for extra insertions
// after each position.  The number of insertions needed after position  i
// is in  insert_size [i] . 

  {
   unsigned  ct;
   int  d_size, d_pos, alloc_size;
   int  leftover_inserts, sum;
   int  i, j, k, m, q;

   d_size = 2 * dp -> diff_len + 5;
   mod_dp -> de = (Diff_Entry_t *) safe_malloc (d_size * sizeof (Diff_Entry_t));
   d_pos = 0;

   sum = 0;
   for (i = 0; i < dp -> a_lo; i ++)
     sum += insert_size [i];
   mod_dp -> a_lo = dp -> a_lo + sum;

   j = dp -> a_lo;
   ct = 0;   // will be length of next Diff_Entry
   leftover_inserts = 0;
   for (k = 0; k < dp -> diff_len; k ++)
     {
      for (m = 0; m < dp -> de [k] . len; m ++)
        {
         for (q = 0; q < leftover_inserts; q ++)
           {
            Set_Diff_Entry (& (mod_dp -> de), & d_pos, & d_size, ct, 1, 0);
                 // character 0 doesn't matter
            ct = 0;
           }
         ct ++;
         leftover_inserts = insert_size [j];
         j ++;  // position in A string
        }

      switch (dp -> de [k] . action)
        {
         case 0 :    // insert becomes substitution
           Set_Diff_Entry (& (mod_dp -> de), & d_pos, & d_size, ct, 2,
                dp -> de [k] . ch);
           leftover_inserts --;
           break;
         case 1 :    // delete
           for (q = 0; q < leftover_inserts; q ++)
             {
              Set_Diff_Entry (& (mod_dp -> de), & d_pos, & d_size, ct, 1, 0);
                   // character 0 doesn't matter
              ct = 0;
             }
           Set_Diff_Entry (& (mod_dp -> de), & d_pos, & d_size, ct, 1, 0);
           leftover_inserts = insert_size [j];
           j ++;
           break;
         case 2 :    // substitute
           Set_Diff_Entry (& (mod_dp -> de), & d_pos, & d_size, ct, 2,
                dp -> de [k] . ch);
           leftover_inserts = insert_size [j];
           j ++;
           break;
         case 3 :    // noop
           // should be end of alignment
           Set_Diff_Entry (& (mod_dp -> de), & d_pos, & d_size, ct, 3, 0);
           j ++;
           break;
        }
      ct = 0;
     }

   for (i = dp -> a_lo; i < dp -> a_hi - 1; i ++)
     sum += insert_size [i];
   mod_dp -> a_hi = dp -> a_hi + sum;
   mod_dp -> diff_len = d_pos;
   if (d_pos == 0)
     alloc_size = 1;
   else
     alloc_size = d_pos;
   mod_dp -> de = (Diff_Entry_t *) safe_realloc (mod_dp -> de,
        alloc_size * sizeof (Diff_Entry_t));
   mod_dp -> b_iid = dp -> b_iid;
   mod_dp -> disregard = dp -> disregard;
   mod_dp -> is_homopoly_type = dp -> is_homopoly_type;

   return;
  }



static void  Output_Correction_Header
  (FILE * fp, int sub)

// Output to  fp  the correction header record for  Frag [sub] .

  {
   Correction_Output_t  out;

   out . frag . is_ID = TRUE;
   out . frag . keep_left = (Frag [sub] . left_degree < Degree_Threshold);
   out . frag . keep_right = (Frag [sub] . right_degree < Degree_Threshold);
   out . frag . iid = Lo_Frag_IID + sub;
   fwrite (& out, sizeof (Correction_Output_t), 1, fp);
   
   return;
  }



static void  Output_Corrections
    (FILE  * fp)

//  Output the corrections in  Frag  to  fp .

  {
   Correction_Output_t  out;
   double  extension_sum = 0.0;
   int  extension_ct = 0;
   int  i, j;

   for  (i = 0;  i < Num_Frags;  i ++)
     {
      int  clear_extension, last_conf;

      out . frag . is_ID = TRUE;
      out . frag . keep_left = (Frag [i] . left_degree < Degree_Threshold);
      out . frag . keep_right = (Frag [i] . right_degree < Degree_Threshold);
      out . frag . iid = Lo_Frag_IID + i;
      fwrite (& out, sizeof (Correction_Output_t), 1, fp);
      if  (Frag [i] . sequence == NULL)
          continue;   // Deleted fragment

      last_conf = Frag [i] . clear_len - 1;
      if  (Extend_Fragments)
          {
           for  (j = Frag [i] . clear_len;  Frag [i] . sequence [j] != 0;  j ++)
             if  (Frag [i] . vote [j] . confirmed > 0)
                 last_conf = j;
             else if  (j - last_conf > 2 * End_Exclude_Len + 1)
                 break;
           clear_extension = 1 + last_conf - Frag [i] . clear_len;
           extension_sum += clear_extension;
           extension_ct ++;
           out . corr . is_ID = FALSE;
           out . corr . pos = clear_extension;
           out . corr . type = (int) EXTENSION;
           fwrite (& out, sizeof (Correction_Output_t), 1, fp);
          }

      for  (j = 0;  j <= last_conf;  j ++)
        {
         Vote_Value_t  vote, ins_vote;
         int  haplo_ct, ins_haplo_ct;
         int  max, total, tmp;
         int  ins_max, ins_total;
         int  is_change = TRUE;

         if  (Frag [i] . vote [j] . confirmed < 2)
             {
              haplo_ct = 0;
              vote = DELETE;
              total = max = Frag [i] . vote [j] . deletes;
              if  (max >= MIN_HAPLO_OCCURS)
                  haplo_ct ++;

              tmp = Frag [i] . vote [j] . a_subst;
              total += tmp;
              if  (tmp > max)
                  {
                   max = tmp;
                   vote = A_SUBST;
                   is_change = (Frag [i] . sequence [j] != 'a');
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  haplo_ct ++;
              
              tmp = Frag [i] . vote [j] . c_subst;
              total += tmp;
              if  (tmp > max)
                  {
                   max = tmp;
                   vote = C_SUBST;
                   is_change = (Frag [i] . sequence [j] != 'c');
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  haplo_ct ++;
              
              tmp = Frag [i] . vote [j] . g_subst;
              total += tmp;
              if  (tmp > max)
                  {
                   max = tmp;
                   vote = G_SUBST;
                   is_change = (Frag [i] . sequence [j] != 'g');
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  haplo_ct ++;
              
              tmp = Frag [i] . vote [j] . t_subst;
              total += tmp;
              if  (tmp > max)
                  {
                   max = tmp;
                   vote = T_SUBST;
                   is_change = (Frag [i] . sequence [j] != 't');
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  haplo_ct ++;

              if  (2 * max > total
                     && total > 1
                     && is_change
                     && (haplo_ct < 2 || ! Use_Haplo_Ct)
                     && (Frag [i] . vote [j] . confirmed == 0
                           || (Frag [i] . vote [j] . confirmed == 1
                               && max > 6)))
                  {
                   out . corr . is_ID = FALSE;
                   out . corr . pos = j;
                   out . corr . type = (int) vote;
                   fwrite (& out, sizeof (Correction_Output_t), 1, fp);
                  }
             }
         if  (Frag [i] . vote [j] . no_insert < 2)
             {
              ins_haplo_ct = 0;
              ins_vote = A_INSERT;
              ins_total = ins_max = Frag [i] . vote [j] . a_insert;
              if  (ins_max >= MIN_HAPLO_OCCURS)
                  ins_haplo_ct ++;

              tmp = Frag [i] . vote [j] . c_insert;
              ins_total += tmp;
              if  (tmp > ins_max)
                  {
                   ins_max = tmp;
                   ins_vote = C_INSERT;
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  ins_haplo_ct ++;
              
              tmp = Frag [i] . vote [j] . g_insert;
              ins_total += tmp;
              if  (tmp > ins_max)
                  {
                   ins_max = tmp;
                   ins_vote = G_INSERT;
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  ins_haplo_ct ++;
              
              tmp = Frag [i] . vote [j] . t_insert;
              ins_total += tmp;
              if  (tmp > ins_max)
                  {
                   ins_max = tmp;
                   ins_vote = T_INSERT;
                  }
              if  (tmp >= MIN_HAPLO_OCCURS)
                  ins_haplo_ct ++;

              if  (2 * ins_max > ins_total
                     && ins_total > 1
                     && (ins_haplo_ct < 2 || ! Use_Haplo_Ct)
                     && (Frag [i] . vote [j] . no_insert == 0
                           || (Frag [i] . vote [j] . no_insert == 1
                               && ins_max > 6)))
                  {
                   out . corr . is_ID = FALSE;
                   out . corr . pos = j;
                   out . corr . type = (int) ins_vote;
                   fwrite (& out, sizeof (Correction_Output_t), 1, fp);
                  }
             }
        }
     }

   fprintf (stderr, "Fragments processed = %d\n", extension_ct);
   if  (Extend_Fragments)
       fprintf (stderr, "   Avg 3' extension = %.1f bases\n",
                extension_ct == 0 ? 0.0 : extension_sum / extension_ct);

   return;
  }



static void  Parse_Command_Line
    (int argc, char * argv [])

//  Get options and parameters from command line with  argc
//  arguments in  argv [0 .. (argc - 1)] .

  {
   int  ch, errflg = FALSE;
   char  * p;

   optarg = NULL;

   while  (! errflg
             && ((ch = getopt (argc, argv, "abc:d:eF:Ghk:o:pS:t:v:V:x:y:z")) != EOF))
     switch  (ch)
       {
        case  'a' :
          Asymmetric_Olaps = TRUE;
          break;

        case  'b' :
          OVL_Output_Type = BINARY_FILE;
          break;

        case  'c' :
          Correction_Filename = optarg;
          break;

        case  'd' :
          Degree_Threshold = (int) strtol (optarg, & p, 10);
          if  (p == optarg)
              {
               fprintf (stderr, "ERROR:  Illegal degree threshold \"%s\"\n",
                        optarg);
               errflg = TRUE;
              }
          break;

        case  'e' :
          Extend_Fragments = TRUE;
          break;

        case  'F' :
          Olap_Path = optarg;
          break;

        case  'G' :
          Doing_Partial_Overlaps = TRUE;
          Doing_Corrections = FALSE;
          break;

        case  'h' :
          errflg = TRUE;
          break;

        case  'k' :
          Kmer_Len = (int) strtol (optarg, & p, 10);
          if  (p == optarg || Kmer_Len <= 1)
              {
               fprintf (stderr, "ERROR:  Illegal k-mer length \"%s\"\n",
                        optarg);
               errflg = TRUE;
              }
          break;

        case  'o' :
          OVL_Output_Path = optarg;
          break;

        case  'p' :
          Use_Haplo_Ct = FALSE;
          break;

        case  'S' :
          Olap_Path = optarg;
          Seeds_From_Store = TRUE;
          break;

        case  't' :
          Num_PThreads = (int) strtol (optarg, & p, 10);
          fprintf (stderr, "Number of pthreads set to %d\n", Num_PThreads);
          break;

        case  'v' :
          Verbose_Level = (int) strtol (optarg, & p, 10);
          fprintf (stderr, "Verbose level set to %d\n", Verbose_Level);
          break;

        case  'V' :
          Vote_Qualify_Len = (int) strtol (optarg, & p, 10);
          fprintf (stderr, "Correction min-match len set to %d\n", Vote_Qualify_Len);
          break;

        case  'x' :
          End_Exclude_Len = (int) strtol (optarg, & p, 10);
          if  (p == optarg || End_Exclude_Len < 0)
              {
               fprintf (stderr, "ERROR:  Illegal end-exclude length \"%s\"\n",
                        optarg);
               errflg = TRUE;
              }
          break;

        case  'y' :
          Error_Rate = strtod (optarg, & p);
          if  (p == optarg || MAX_ERROR_RATE < Error_Rate)
              {
               fprintf (stderr, "ERROR:  Bad error rate  %s\n", optarg);
               fprintf (stderr, "Value must be between 0.0 and %.3f\n",
                    MAX_ERROR_RATE);
               errflg = TRUE;
              }
          break;

        case  'z' :
          Doing_Corrections = FALSE;
          break;

        case  '?' :
          fprintf (stderr, "Unrecognized option -%c\n", optopt);

        default :
          errflg = TRUE;
       }

   if  (errflg || optind != argc - 3)
       {
        Usage (argv [0]);
        exit (EXIT_FAILURE);
       }

   if  (Olap_Path == NULL)
       {
        fprintf (stderr, "ERROR:  Must specify overlaps with -F or -S\n");
        exit (EXIT_FAILURE);
       }

   gkpStore_Path = argv [optind ++];

   Lo_Frag_IID = (int) strtol (argv [optind], & p, 10);
   if  (p == optarg || Lo_Frag_IID < 1)
       {
        fprintf (stderr, "ERROR:  Illegal low fragment IID \"%s\"\n",
                 argv [optind]);
        Usage (argv [0]);
        exit (EXIT_FAILURE);
       }
   optind ++;

   if  (strcmp (argv [optind], "end") == 0)
       {
        Hi_Frag_IID = INT_MAX;
        p = NULL;
       }
     else
       Hi_Frag_IID = (int) strtol (argv [optind], & p, 10);
   if  (p == argv [optind] || Hi_Frag_IID < Lo_Frag_IID)
       {
        fprintf (stderr, "ERROR:  Illegal high fragment IID \"%s\"\n",
                 argv [optind]);
        Usage (argv [0]);
        exit (EXIT_FAILURE);
       }

   return;
  }


static void  Output_Olap
  (Olap_Info_t * olap, int a_lo, int a_hi, int a_len,
   int b_lo, int b_hi, int b_len, int errors)

// Output the overlap between the reads in  olap
// where positions  a_lo .. a_hi  in read A match positions  b_lo .. b_hi
// in read B.   a_len  and  b_len  are the lengths of reads A and B, respectively.
//  errors  is the number of edits to make the match positions align.

  {
   OVSoverlap  overlap;
   double  qual;
   char  dir;
   int  x, y;

   if (Asymmetric_Olaps && olap -> b_iid <= olap -> a_iid)
     return;

   if (olap -> orient == INNIE)
     {
      dir = 'r';
      x = b_len - b_lo;
      y = b_len - b_hi;
     }
   else
     {
      dir = 'f';
      x = b_lo;
      y = b_hi;
     }
   qual = errors / (double)OVL_Min_int (a_hi - a_lo, b_hi - b_lo);


   switch (OVL_Output_Type)
     {
      case TEXT_FILE :
        if (Num_PThreads > 0)
           pthread_mutex_lock (& Print_Mutex);
        fprintf (OVL_Output_fp, "%7d %7d  %c %4d %4d %4d  %4d %4d %4d  %5.2f\n",
             olap -> a_iid, olap -> b_iid, dir, a_lo, a_hi, a_len,
             x, y, b_len, qual * 100.0);
        if (Num_PThreads > 0)
           pthread_mutex_unlock (& Print_Mutex);
        break;

      case BINARY_FILE :
        overlap . a_iid = olap -> a_iid;
        overlap . b_iid = olap -> b_iid;

        if (Doing_Partial_Overlaps)
          {
           overlap . dat . obt . a_beg = a_lo;
           overlap . dat . obt . a_end = a_hi;
           overlap . dat . obt . fwd = (dir == 'f' ? 1 : 0);
           overlap . dat . obt . b_beg = x;
           overlap . dat . obt . b_end = y;
           overlap . dat . obt . erate = AS_OVS_encodeQuality (qual);
           overlap . dat . obt . type = AS_OVS_TYPE_OBT;
          }
        else
          {
           overlap . dat . ovl . seed_value = olap -> k_count;
           overlap . dat . ovl . flipped = (dir == 'f' ? 0 : 1);

           if (0 < a_lo)
             overlap . dat . ovl . a_hang = a_lo;
           else
             overlap . dat . ovl . a_hang = - b_lo;   // negative a_hang

           if (a_hi < a_len)
             overlap . dat . ovl . b_hang = a_hi - a_len;   // negative b_hang
           else
             overlap . dat . ovl . b_hang = b_len - b_hi;

           overlap . dat . ovl . orig_erate = overlap . dat . ovl . corr_erate
                = AS_OVS_encodeQuality (qual);
           overlap . dat . ovl . type = AS_OVS_TYPE_OVL;
          }

        if (Num_PThreads > 0)
           pthread_mutex_lock (& Print_Mutex);
        AS_OVS_writeOverlap (Binary_OVL_Output_fp, & overlap);
        if (Num_PThreads > 0)
           pthread_mutex_unlock (& Print_Mutex);
        break;
     }

   return;
  }


static void  Output_Olap_From_Diff
  (const Sequence_Diff_t * dp, int sub)

// Output the overlap described in  dp  which is wrt read  FRag [sub] .

  {
   OVSoverlap  overlap;
   double  qual;
   char  dir;
   int32  a_iid, a_len;
   int  x, y, errors;

   a_iid = Lo_Frag_IID + sub;
   if (Asymmetric_Olaps && dp -> b_iid <= a_iid)
     return;

   a_len = Frag [sub] . len;
   if (dp -> flipped)
     {
      dir = 'r';
      x = dp -> b_len - dp -> b_lo;
      y = dp -> b_len - dp -> b_hi;
     }
   else
     {
      dir = 'f';
      x = dp -> b_lo;
      y = dp -> b_hi;
     }

   errors = dp -> diff_len;
   if (errors > 0 && dp -> de [errors - 1] . action == 3)  // don't count noop
     errors --;
   qual = errors / (double)OVL_Min_int (dp -> a_hi - dp -> a_lo,
        dp -> b_hi - dp -> b_lo);

   switch (OVL_Output_Type)
     {
      case TEXT_FILE :
        if (Num_PThreads > 0)
           pthread_mutex_lock (& Print_Mutex);
        fprintf (OVL_Output_fp, "%7d %7d  %c %4d %4d %4d  %4d %4d %4d  %5.2f\n",
             a_iid, dp -> b_iid, dir, dp -> a_lo, dp -> a_hi, a_len,
             x, y, dp -> b_len, qual * 100.0);
        if (Num_PThreads > 0)
           pthread_mutex_unlock (& Print_Mutex);
        break;

      case BINARY_FILE :
        overlap . a_iid = a_iid;
        overlap . b_iid = dp -> b_iid;

        if (Doing_Partial_Overlaps)
          {
           overlap . dat . obt . a_beg = dp -> a_lo;
           overlap . dat . obt . a_end = dp -> a_hi;
           overlap . dat . obt . fwd = (dir == 'f' ? 1 : 0);
           overlap . dat . obt . b_beg = x;
           overlap . dat . obt . b_end = y;
           overlap . dat . obt . erate = AS_OVS_encodeQuality (qual);
           overlap . dat . obt . type = AS_OVS_TYPE_OBT;
          }
        else
          {
           overlap . dat . ovl . seed_value = dp -> seed_value;
           overlap . dat . ovl . flipped = (dir == 'f' ? 0 : 1);

           if (0 < dp -> a_lo)
             overlap . dat . ovl . a_hang = dp -> a_lo;
           else
             overlap . dat . ovl . a_hang = - dp -> b_lo;   // negative a_hang

           if (dp -> a_hi < a_len)
             overlap . dat . ovl . b_hang = dp -> a_hi - a_len;   // negative b_hang
           else
             overlap . dat . ovl . b_hang = dp -> b_len - dp -> b_hi;

           overlap . dat . ovl . orig_erate = overlap . dat . ovl . corr_erate
                = AS_OVS_encodeQuality (qual);
           overlap . dat . ovl . type = AS_OVS_TYPE_OVL;
          }

        if (Num_PThreads > 0)
           pthread_mutex_lock (& Print_Mutex);
        AS_OVS_writeOverlap (Binary_OVL_Output_fp, & overlap);
        if (Num_PThreads > 0)
           pthread_mutex_unlock (& Print_Mutex);
        break;
     }

   return;
  }



#if 0   //**ALD Not used???
static int  Prefix_Edit_Dist
    (char A [], int m, char T [], int n, int Error_Limit,
     int * A_End, int * T_End, int * Match_To_End,
     int Delta [MAX_ERRORS], int * Delta_Len, Thread_Work_Area_t * wa)

//  Return the minimum number of changes (inserts, deletes, replacements)
//  needed to match string  A [0 .. (m-1)]  with a prefix of string
//   T [0 .. (n-1)]  if it's not more than  Error_Limit .
//  Put delta description of alignment in  Delta  and set
//  (* Delta_Len)  to the number of entries there if it's a complete
//  match.
//  Set  A_End  and  T_End  to the rightmost positions where the
//  alignment ended in  A  and  T , respectively.
//  Set  Match_To_End  true if the match extended to the end
//  of at least one string; otherwise, set it false to indicate
//  a branch point.
//  (* wa) has storage used by this thread

  {
   double  Score, Max_Score;
   int  Max_Score_Len, Max_Score_Best_d, Max_Score_Best_e;
#if 0
   int Tail_Len;
#endif
   int  Best_d, Best_e, Longest, Row;
   int  Left, Right;
   int  d, e, j, shorter;

//   assert (m <= n);
   Best_d = Best_e = Longest = 0;
   (* Delta_Len) = 0;

   shorter = OVL_Min_int (m, n);
   for  (Row = 0;  Row < shorter && A [Row] == T [Row];  Row ++)
     ;

   wa -> edit_array [0] [0] = Row;

   if  (Row == shorter)                              // Exact match
       {
        (* A_End) = (* T_End) = Row;
        (* Match_To_End) = TRUE;
        return  0;
       }

   Left = Right = 0;
   Max_Score = 0.0;
   Max_Score_Len = Max_Score_Best_d = Max_Score_Best_e = 0;
   for  (e = 1;  e <= Error_Limit;  e ++)
     {
      Left = OVL_Max_int (Left - 1, -e);
      Right = OVL_Min_int (Right + 1, e);
      wa -> edit_array [e - 1] [Left] = -2;
      wa -> edit_array [e - 1] [Left - 1] = -2;
      wa -> edit_array [e - 1] [Right] = -2;
      wa -> edit_array [e - 1] [Right + 1] = -2;

      for  (d = Left;  d <= Right;  d ++)
        {
         Row = 1 + wa -> edit_array [e - 1] [d];
         if  ((j = wa -> edit_array [e - 1] [d - 1]) > Row)
             Row = j;
         if  ((j = 1 + wa -> edit_array [e - 1] [d + 1]) > Row)
             Row = j;
         while  (Row < m && Row + d < n
                  && A [Row] == T [Row + d])
           Row ++;

         wa -> edit_array [e] [d] = Row;

         if  (Row == m || Row + d == n)
             {
#if  1
              // Force last error to be mismatch rather than insertion
              if  (Row == m
                     && 1 + wa -> edit_array [e - 1] [d + 1]
                          == wa -> edit_array [e] [d]
                     && d < Right)
                  {
                   d ++;
                   wa -> edit_array [e] [d] = wa -> edit_array [e] [d - 1];
                  }
#endif
              (* A_End) = Row;           // One past last align position
              (* T_End) = Row + d;

              Compute_Delta
                  (Delta, Delta_Len, wa -> edit_array, e, d, Row);

#if  0
              //  Check for branch point here caused by uneven
              //  distribution of errors

              Score = Row * BRANCH_PT_MATCH_VALUE - e;
                        // Assumes  BRANCH_PT_MATCH_VALUE
                        //             - BRANCH_PT_ERROR_VALUE == 1.0
              Tail_Len = Row - Max_Score_Len;
              if  (e > MIN_BRANCH_END_DIST / 2
                       && Tail_Len >= MIN_BRANCH_END_DIST
                       && (Max_Score - Score) / Tail_Len >= MIN_BRANCH_TAIL_SLOPE)
                  {
                   (* A_End) = Max_Score_Len;
                   (* T_End) = Max_Score_Len + Max_Score_Best_d;
                   (* Match_To_End) = FALSE;
                   return  Max_Score_Best_e;
                  }
#endif

              (* Match_To_End) = TRUE;
              return  e;
             }
        }

      while  (Left <= Right && Left < 0
                  && wa -> edit_array [e] [Left] < Edit_Match_Limit [e])
        Left ++;
      if  (Left >= 0)
          while  (Left <= Right
                    && wa -> edit_array [e] [Left] + Left < Edit_Match_Limit [e])
            Left ++;
      if  (Left > Right)
          break;
      while  (Right > 0
                  && wa -> edit_array [e] [Right] + Right < Edit_Match_Limit [e])
        Right --;
      if  (Right <= 0)
          while  (wa -> edit_array [e] [Right] < Edit_Match_Limit [e])
            Right --;
      assert (Left <= Right);

      for  (d = Left;  d <= Right;  d ++)
        if  (wa -> edit_array [e] [d] > Longest)
            {
             Best_d = d;
             Best_e = e;
             Longest = wa -> edit_array [e] [d];
            }
#if  1
      Score = Longest * BRANCH_PT_MATCH_VALUE - e;
               // Assumes  BRANCH_PT_MATCH_VALUE - BRANCH_PT_ERROR_VALUE == 1.0
      if  (Score > Max_Score
               && Best_e <= Error_Bound [OVL_Min_int (Longest, Longest + Best_d)])
          {
           Max_Score = Score;
           Max_Score_Len = Longest;
           Max_Score_Best_d = Best_d;
           Max_Score_Best_e = Best_e;
          }
#endif
     }

   Compute_Delta
       (Delta, Delta_Len, wa -> edit_array, Max_Score_Best_e,
        Max_Score_Best_d, Max_Score_Len);

   (* A_End) = Max_Score_Len;
   (* T_End) = Max_Score_Len + Max_Score_Best_d;
   (* Match_To_End) = FALSE;

   return  Max_Score_Best_e;
  }
#endif



static void  Process_Olap
  (Olap_Info_t * olap, char * b_seq, unsigned b_len, char * rev_seq,
   int * rev_id, int shredded, int is_homopoly, Thread_Work_Area_t * wa)

// Find the alignment referred to in  olap , where the  a_iid
// fragment is in  Frag  and the  b_iid  sequence is in  b_seq .
//  b_len  is the length of  b_seq .
// Use the alignment to increment the appropriate vote fields
// for the a fragment.   shredded  is true iff the b fragment
// is from shredded data, in which case the overlap will be
// ignored if the a fragment is also shredded.
//  is_homopoly  indicates if the b fragment has untrusted homopolymer
// run lengths.
// rev_seq  is a buffer to hold the reverse complement of  b_seq
// if needed.  (* rev_id) is used to keep track of whether
// rev_seq  has been created yet.  (* wa) is the work-area
// containing space for the process to use in case of multi-threading.

  {
   char  * a_part, * b_part;
   unsigned  a_len;
   int  right_delta [MAX_ERRORS], right_delta_len;
   int  left_delta [MAX_ERRORS], left_delta_len;
   int  a_part_len, b_part_len, a_end, b_end, olap_len;
   int  a_lo, a_hi, a_match_len, b_lo, b_hi, b_match_len;
   int  left_errors, right_errors, leftover, match_to_end;
   int  a_offset, b_offset, allowed_errors, remaining_errors, sub;
   int  i;

   if (Verbose_Level > 0)
      printf ("Process_Olap:  %8d %8d %5d %5d  %c\n",
           olap -> a_iid, olap -> b_iid,
           olap -> a_hang, olap -> b_hang,
           olap -> orient == INNIE ? 'I' : 'N');

   sub = olap -> a_iid - Lo_Frag_IID;

//**ALD This should be changed maybe so that the overlaps are allowed,
//      but not for correction purposes
   if (shredded && Frag [sub] . shredded)
      return;

   // Get pointers to the beginning of the seed match region
   if (Offsets_WRT_Raw)
      a_offset = olap -> a_hang - Frag [sub] . trim_5p;
   else
      a_offset = olap -> a_hang;
   a_part = Frag [sub] . sequence + a_offset;

   // olap -> b_hang has already been adjusted for the clear
   // range if necessary
   if (olap -> orient == NORMAL)
     {
      b_offset = olap -> b_hang;
      b_part = b_seq + b_offset;
     }
   else
     {
      if  ((* rev_id) != olap -> b_iid)
          {
           strcpy (rev_seq, b_seq);
           Rev_Complement (rev_seq);
           (* rev_id) = olap -> b_iid;
          }
      b_offset = olap -> b_hang;
      b_part = rev_seq + b_offset;
     }

   if (Verbose_Level > 0)
      printf ("b_part = %p  is ascii %d  rev_seq is %d\n",
           b_part, (int) (* b_part), (int) (* rev_seq));
   if (! isalpha (* b_part) || ! isalpha (* rev_seq))
      exit (-1);

   if (Verbose_Level > 0)
     {
      int  j, ct;

      printf (">a_part\n");
      ct = 0;
      for (j = 0; a_part [j] != '\0'; j ++)
        {
         if (ct == 60)
           {
            putchar ('\n');
            ct = 0;
           }
         if (j + a_offset >= Frag [sub] . clear_len )
            putchar (toupper (a_part [j]));
         else
            putchar (a_part [j]);
         ct ++;
        }
      putchar ('\n');

      printf (">b_part\n");
      ct = 0;
      for (j = 0; b_part [j] != '\0'; j ++)
        {
         if (ct == 60)
           {
            putchar ('\n');
            ct = 0;
           }
         putchar (b_part [j]);
         ct ++;
        }
      putchar ('\n');
     }

   // Check that there's at least one letter of exact match here
   if (* a_part != * b_part)
     {
      fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
      fprintf (stderr, "Bad seed position--letters differ here\n");
      fprintf (stderr, "a_iid=%d  b_iid=%d  a_offset=%d  b_offset=%d  ori=%c"
           "  a_ch=%c  b_ch=%d\n", olap -> a_iid, olap -> b_iid,
           olap -> a_hang, olap -> b_hang, olap -> orient == INNIE ? 'I' : 'N',
           * a_part, * b_part);
      exit (EXIT_FAILURE);
     }

   // Try to extend the alignment forward from the exact match
   a_part_len = strlen (a_part);
   b_part_len = strlen (b_part);
   a_len = a_offset + a_part_len;
   olap_len = OVL_Min_int (a_part_len, b_part_len)
        + OVL_Min_int (a_offset, b_offset);
   allowed_errors = Error_Bound [olap_len];

   right_errors = Fwd_Prefix_Edit_Dist (a_part, a_part_len,
        b_part, b_part_len, allowed_errors, & a_end, & b_end,
        & match_to_end, Char_Match_Value, right_delta,
        & right_delta_len, wa -> edit_array, Edit_Match_Limit, Error_Bound,
        Doing_Partial_Overlaps);

   if (a_end < 0 || a_end > a_part_len || b_end < 0 || b_end > b_part_len)
     {
      fprintf (stderr, "ERROR:  Bad edit distance\n");
      fprintf (stderr, "errors = %d  a_end = %d  b_end = %d\n",
               right_errors, a_end, b_end);
      fprintf (stderr, "a_part_len = %d  b_part_len = %d\n",
               a_part_len, b_part_len);
      fprintf (stderr, "a_iid = %d  b_iid = %d  match_to_end = %c\n",
               olap -> a_iid, olap -> b_iid, match_to_end ? 'T' : 'F');
      exit (-3);
     }

   if (Verbose_Level > 1)
     {
      int  j;

      printf ("Fwd:  errors/allowed = %d/%d  right_delta_len = %d  %s\n",
           right_errors, allowed_errors, right_delta_len,
           (0 < right_delta_len ? "Deltas:" : ""));
      for (j = 0; j < right_delta_len; j ++)
        printf (" %5d\n", right_delta [j]);
      printf ("  match_to_end = %c\n", (match_to_end ? 'T' : 'F'));
      printf ("  a_align = %d/%d  b_align = %d/%d\n", a_end, a_part_len,
           b_end, b_part_len);
      Display_Alignment (a_part, a_end, b_part,
           b_end, right_delta, right_delta_len, a_len - a_offset);
      if (Verbose_Level > 2)
         Show_Edit_Array (wa -> edit_array, right_errors);
     }

   // If we're trying to extend past the clear range, a match past that point
   // counts as a match to the end
   if (! match_to_end && a_end + a_offset >= Frag [sub] . clear_len - 1)
     match_to_end = TRUE;

   remaining_errors = allowed_errors - right_errors;
   if (! Doing_Partial_Overlaps && (! match_to_end || remaining_errors < 0))
     {
      wa -> failed_olaps ++;
      return;
     }
   a_match_len = a_end;
   b_match_len = b_end;

   // Now try to extend the alignment backward from the exact match

   left_errors = Rev_Prefix_Edit_Dist (a_part - 1, a_offset,
        b_part - 1, b_offset, remaining_errors, & a_end, & b_end,
        & leftover, & match_to_end, Char_Match_Value, left_delta,
        & left_delta_len, wa -> edit_array, Edit_Match_Limit, Error_Bound,
        Doing_Partial_Overlaps);

   if (Verbose_Level > 1)
     {
      int  j;

      printf ("Rev:  errors/remaining = %d/%d  left_delta_len = %d  %s\n",
           left_errors, remaining_errors, left_delta_len, 
           (0 < left_delta_len ? "Deltas:" : ""));
      for (j = 0; j < left_delta_len; j ++)
        printf (" %5d\n", left_delta [j]);
      printf ("  match_to_end = %c\n", (match_to_end ? 'T' : 'F'));
      printf ("  a_offset/a_end = %d/%d  b_offset/b_end = %d/%d\n",
           a_offset, a_end, b_offset, b_end);
      printf ("leftover = %d\n", leftover);
      Display_Alignment (a_part + a_end, - a_end,
           b_part + b_end, - b_end, left_delta,
           left_delta_len, - a_end);
      if (Verbose_Level > 2)
         Show_Edit_Array (wa -> edit_array, left_errors);
     }

   if (! Doing_Partial_Overlaps && (! match_to_end || remaining_errors < left_errors))
     {
      wa -> failed_olaps ++;
      return;
     }

//**ALD Don't need to combine delta's if going to recompute the alignment
// Even better would be to do reverse alignment first, and then
// complete forward alignment from that position.
   // Combine the two delta encodings
   // First add the leftover length and exact-match length to
   // the first right_delta entry
   if (0 < right_delta_len)
     {
      if (0 < right_delta [0])
        left_delta [left_delta_len ++] = right_delta [0] + leftover;
      else
        left_delta [left_delta_len ++] = right_delta [0] - leftover;
     }
   // Then append the remaining right_delta entries onto left_delta
   for (i = 1; i < right_delta_len; i ++)
     left_delta [left_delta_len ++] = right_delta [i];

   a_match_len -= a_end;
   b_match_len -= b_end;
   olap_len = OVL_Min_int (a_match_len, b_match_len);
   if (Verbose_Level > 0)
     {
      int  j;

      printf ("Combined:  errors = %d  left_delta_len = %d  %s\n",
           left_errors + right_errors, left_delta_len,
           (0 < left_delta_len ? "Deltas:" : ""));
      for (j = 0; j < left_delta_len; j ++)
        printf (" %5d\n", left_delta [j]);
      printf ("  match_to_end = %c  olap_len = %d\n",
           (match_to_end ? 'T' : 'F'), olap_len);
      printf ("  a_begin/match_len = %d/%d  b_begin/match_len = %d/%d\n",
           a_offset + a_end, a_match_len, b_offset + b_end, b_match_len);
      Display_Alignment (a_part + a_end, a_match_len, b_part + b_end,
           b_match_len, left_delta, left_delta_len, a_match_len);
     }

   a_lo = a_offset + a_end;
   a_hi = a_lo + a_match_len;
   b_lo = b_offset + b_end;
   b_hi = b_lo + b_match_len;
   if (OVL_Min_int (a_hi - a_lo, b_hi - b_lo) < Min_Olap_Len)
     {
      wa -> failed_olaps ++;
      return;
     }

   if (left_errors + right_errors <= Error_Bound [olap_len]
        && (Doing_Partial_Overlaps || match_to_end))
     {
#if USE_NEW_STUFF
      Sequence_Diff_t  diff;
      int  new_delta [MAX_ERRORS];
      int  new_errors, new_a_end, new_b_end, new_delta_len, new_match_to_end;
      int  k;

      // Redo the alignment to make it consistent.  The problem is that
      // the current delta was constructed in two directions out from
      // the initial seed.  Thus an insert at the end of a homopolymer
      // run will sometimes be on the left of the run and sometimes on the
      // right.  We want them all on the same end.
//**ALD
if (0)
{
 Homopoly_Match_Entry_t  hp_space [40 * 40];
 Homopoly_Match_Entry_t  * hp_array [40];
 int  i;

 for (i = 0; i < 40;  i ++)
   hp_array [i] = hp_space + i * i;
 new_errors = Fwd_Homopoly_Prefix_Match (a_part + a_end, a_part_len - a_end,
      b_part + b_end, b_part_len - b_end, left_errors + right_errors + 1,
      & new_a_end, & new_b_end, & new_match_to_end, new_delta,
      & new_delta_len, hp_array);
 printf ("Homopoly alignment:  a_lo/hi=%d/%d  b_lo/hi=%d/%d\n", a_lo, a_lo + new_a_end,
      b_lo, b_lo + new_b_end);
// Display_Alignment (a_part + a_end, new_a_end, b_part + b_end, new_b_end,
//      new_delta, new_delta_len, new_a_end);
}
      new_errors = Fwd_Prefix_Edit_Dist (a_part + a_end, a_part_len - a_end,
           b_part + b_end, b_part_len - b_end, left_errors + right_errors + 1,
           & new_a_end, & new_b_end, & new_match_to_end, Char_Match_Value, new_delta,
           & new_delta_len, wa -> edit_array, Edit_Match_Limit, Error_Bound,
           Doing_Partial_Overlaps);

//**ALD
#if 0
printf ("orig delta len = %d\n", left_delta_len);
for (i = 0; i < left_delta_len; i ++)
  printf ("%3d: %+4d\n", i, left_delta [i]);
printf ("new delta len = %d\n", new_delta_len);
for (i = 0; i < new_delta_len; i ++)
  printf ("%3d: %+4d\n", i, new_delta [i]);
printf ("Orig alignment:\n");
Display_Alignment (a_part + a_end, a_match_len, b_part + b_end, b_match_len,
     left_delta, left_delta_len, a_match_len);
#endif
//**ALD
#if 0
printf ("New alignment:  a_lo/hi=%d/%d  b_lo/hi=%d/%d\n", a_lo, a_lo + new_a_end,
     b_lo, b_lo + new_b_end);
Display_Alignment (a_part + a_end, new_a_end, b_part + b_end, new_b_end,
     new_delta, new_delta_len, new_a_end);
Show_Edit_Array (wa -> edit_array, new_errors);
#endif

      
      diff . a_lo = a_lo;
      diff . a_hi = a_lo + new_a_end;
      diff . b_lo = b_lo;
      diff . b_hi = b_lo + new_b_end;
      Convert_Delta_To_Diff (new_delta, new_delta_len, a_part + a_end,
           b_part + b_end, new_a_end, new_b_end, & diff,
           left_errors + right_errors, wa);

      k = Frag [sub] . num_diffs ++;
      Frag [sub] . diff_list = safe_realloc (Frag [sub] . diff_list,
           Frag [sub] . num_diffs * sizeof (Sequence_Diff_t));
      Frag [sub] . diff_list [k] = diff;
      Frag [sub] . diff_list [k] . b_iid = olap -> b_iid;
      Frag [sub] . diff_list [k] . disregard = 0;
      Frag [sub] . diff_list [k] . is_homopoly_type = is_homopoly;
      Frag [sub] . diff_list [k] . b_len = b_len;
      Frag [sub] . diff_list [k] . flipped = (olap -> orient == INNIE);
      Frag [sub] . diff_list [k] . seed_value = olap -> k_count;
//**ALD
#if 0
{
 printf ("Diffs:\n");
 for (i = 0; i < diff . diff_len; i ++)
   printf ("%3d: %5u %2u %2u\n", i, diff . de [i] . len,
        diff . de [i] . action, diff . de [i] . ch);
}
#endif
#else   // Old Stuff
      Output_Olap (olap, a_lo, a_hi, a_len, b_lo, b_hi, b_len,
           left_errors + right_errors);

      if (Doing_Corrections)
        Analyze_Alignment (left_delta, left_delta_len, a_part + a_end, b_part + b_end,
             a_match_len, b_match_len, a_lo, sub);
#endif
     }
   else
      wa -> failed_olaps ++;
                      
   return;
  }



static void  Read_Frags
    (void)

//  Open and read fragments with IIDs from  Lo_Frag_IID  to
//  Hi_Frag_IID  from  gkpStore_Path  and store them in
//  global  Frag .

  {
   char  seq_buff [AS_READ_MAX_LEN + 1];
   static  fragRecord  * frag_read = NULL;
   unsigned  clear_start, clear_end;
   int32  high_store_frag;
   int  i, j;

   high_store_frag = getLastElemFragStore (gkpStore);
   if  (Hi_Frag_IID == INT_MAX)
       Hi_Frag_IID = high_store_frag;
   if  (Hi_Frag_IID > high_store_frag)
       {
        fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
        fprintf (stderr, "Hi frag %d is past last store frag %d\n",
                 Hi_Frag_IID, high_store_frag);
        exit (EXIT_FAILURE);
       }

   Num_Frags = 1 + Hi_Frag_IID - Lo_Frag_IID;
   Frag = (Frag_Info_t *) safe_calloc (Num_Frags, sizeof (Frag_Info_t));

   if (frag_read == NULL)
     frag_read = new_fragRecord ();

#ifdef USE_STORE_DIRECTLY_READ
  Internal_gkpStore = openGateKeeperStore (gkpStore_Path, FALSE);
  assert (Internal_gkpStore != NULL);
#else
   Internal_gkpStore
       = loadGateKeeperStorePartial (gkpStore_Path, Lo_Frag_IID, Hi_Frag_IID);
#endif
   
   Frag_Stream = openFragStream (Internal_gkpStore, FRAG_S_SEQ);
   resetFragStream (Frag_Stream, Lo_Frag_IID, Hi_Frag_IID);

   for (i = 0; nextFragStream (Frag_Stream, frag_read); i ++)
     {
      char  * seq_ptr;
      int  raw_len, result, frag_len;

      if (getFragRecordIsDeleted (frag_read))
        {
         Frag [i] . sequence = NULL;
         Frag [i] . vote = NULL;
         continue;
        }

      Frag [i] . shredded = FALSE;
        // Used in Process_Olap to ignore overlaps between two "shredded" reads
        // Perhaps should check for external reads now


      clear_start = getFragRecordClearRegionBegin (frag_read, AS_READ_CLEAR_OBT);
      clear_end = getFragRecordClearRegionEnd (frag_read, AS_READ_CLEAR_OBT);
      raw_len = getFragRecordSequenceLength (frag_read);
      if (AS_READ_MAX_LEN < raw_len)
        {
         fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
         fprintf (stderr, "Read %u is too long:  %d bp; max is %d\n",
              getFragRecordIID (frag_read), raw_len, AS_READ_MAX_LEN);
         exit (-1);
        }

      seq_ptr = getFragRecordSequence (frag_read);
      Frag [i] . trim_5p = clear_start;
      Frag [i] . trim_3p = raw_len - clear_end;
      Frag [i] . clear_len = clear_end - clear_start;
      Frag [i] . is_homopoly_type = Is_Homopoly_Type (frag_read, Internal_gkpStore);

      // Make sure that we have a valid lowercase sequence string

      if  (Extend_Fragments)
          frag_len = raw_len;
        else
          frag_len = clear_end;
      for  (j = clear_start;  j < frag_len;  j ++)
         seq_buff [j - clear_start] = Filter (seq_ptr [j]);
      seq_buff [frag_len - clear_start] = '\0';

      Frag [i] . sequence = strdup (seq_buff);
      Frag [i] . len = frag_len;
      Frag [i] . vote = (Vote_Tally_t *) safe_calloc (frag_len - clear_start,
           sizeof (Vote_Tally_t));
      Frag [i] . left_degree = Frag [i] . right_degree = 0;

#if USE_NEW_STUFF
      Frag [i] . num_diffs = 0;
      Frag [i] . diff_list = NULL;
#endif
     }

   closeFragStream (Frag_Stream);
   closeGateKeeperStore (Internal_gkpStore);

   return;
  }



static void  Read_Seeds
  (void)

//  Open and read those seeds with first IIDs from  Lo_Frag_IID  to
//  Hi_Frag_IID  from  Olap_Path  and store them in
//  global  Olaps .  If  Olap_From_Store  is true, then the overlaps
//  are read from a binary overlap store; otherwise, they are from
//  a text file in the format produced by
//  get-olaps and each overlap must appear twice, once in each order.

  {
   FILE  * fp;
   int32  a_iid, b_iid;
   int  a_offset, b_offset;
   char  orient [10];
   int  count, len;
   long int  olap_size;
   long int  ct = 0;


   if (Seeds_From_Store)
      Get_Seeds_From_Store (Olap_Path, Lo_Frag_IID, Hi_Frag_IID,
           & Olap, & Num_Olaps);
   else
     {
      char  line [MAX_LINE];
      olap_size = 1000;
      Olap = (Olap_Info_t *) safe_malloc (olap_size * sizeof (Olap_Info_t));

      fp = File_Open (Olap_Path, "r");

      while (fgets (line, MAX_LINE, fp) != NULL)
        {
         if (sscanf (line, "%d %d %s %d %d %d %d", & a_iid, & b_iid, orient,
                 & a_offset, & b_offset, & count, & len) >= 7
                 && Lo_Frag_IID <= a_iid && a_iid <= Hi_Frag_IID)
           {
            if (ct >= olap_size)
              {
               olap_size *= EXPANSION_FACTOR;
               Olap = (Olap_Info_t *) safe_realloc (Olap,
                          olap_size * sizeof (Olap_Info_t));
              }
            Olap [ct] . a_iid = a_iid;
            Olap [ct] . b_iid = b_iid;
            Olap [ct] . a_hang = a_offset;
            Olap [ct] . b_hang = b_offset;
            if (tolower (orient [0]) == 'f')
               Olap [ct] . orient = NORMAL;
            else
               Olap [ct] . orient = INNIE;
            Olap [ct] . k_count = 0;
            ct ++;
           }

         if (a_iid > Hi_Frag_IID)   // Speed up if file is sorted
            break;
        }

        Num_Olaps = ct;
        fclose (fp);

        if (Num_Olaps == 0)
          {
           fprintf (stderr, "No overlaps read, nothing to do\n");
           exit (EXIT_FAILURE);
          }

        Olap = (Olap_Info_t *) safe_realloc (Olap, Num_Olaps * sizeof (Olap_Info_t));
       }

   return;
  }



static void  Rev_Complement
    (char * s)

/* Set string  s  to its DNA reverse complement. */

  {
   char  ch;
   int  i, j, len;

   len = strlen (s);

   for  (i = 0, j = len - 1;  i < j;  i ++, j --)
     {
      ch = Complement (s [i]);
      s [i] = Complement (s [j]);
      s [j] = ch;
     }

   if  (i == j)
       s [i] = Complement (s [i]);

   return;
  }



static void  Set_Diff_Entry
  (Diff_Entry_t * * de, int * pos, int * size, unsigned len, unsigned action,
   unsigned ch)

// Set entry  (* de) [* pos]  to  <len,action,ch>  and increment  (* pos)
// If the entry is past the end of  de  (which has  (* size)  entries)
// then increase  (* size)  and allocate more room in  de .

  {
   if ((* pos) >= (* size))
     {
      (* size) *= 2;
      (* de) = (Diff_Entry_t *) safe_realloc (* de, (* size) * sizeof (Diff_Entry_t));
     }

   (* de) [* pos] . len = len;
   (* de) [* pos] . action = action;
   (* de) [* pos] . ch = ch;
   (* pos) ++;

   return;
  }



static void  Set_Homopoly_Votes_From_Diffs
  (int sub, Sequence_Diff_t * dp)

// Set homopoly values in votes in  Frag [sub]  from the alignment entries in  dp .

  {
   Vote_Tally_t  * hp;   // keeps track of first base of homopoly run
   char  hp_ch = 'x';
   int  do_subst;
   int  insert_ct = 0;
   int  hp_len = 0;      // length of current homopoly run
   int  j, k, m;

   j = dp -> a_lo;
   hp = NULL;

   for (k = 0; k < dp -> diff_len; k ++)
     {
      for (m = 0; m < dp -> de [k] . len; m ++)
        {
         if (Frag [sub] . sequence [j] == hp_ch)
           hp_len ++;
         else
           {
            if (hp != NULL)
              Add_Homopoly_Vote (hp, hp_len);
            hp_len = 1;
            hp = Frag [sub] . vote + j;
            hp_ch = Frag [sub] . sequence [j];
           }
         if (dp -> a_lo < j && insert_ct == 0)
           Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
         j ++;
         insert_ct = 0;
        }
      switch (dp -> de [k] . action)
        {
         case 0 :    // insert
           if (Char_Matches (hp_ch, dp -> de [k] . ch))
              hp_len ++;
           else
             {
              if (hp != NULL)
                Add_Homopoly_Vote (hp, hp_len);
              hp = NULL;
              hp_len = 0;
              hp_ch = 'x';
              if (insert_ct ++ == 0 && dp -> a_lo < j)
                Cast_Insert_Vote (Frag [sub] . vote + j - 1, dp -> de [k] . ch);
             }
           break;
         case 1 :    // delete
           if (hp != NULL)
             Add_Homopoly_Vote (hp, hp_len);
           if (hp_ch == Frag [sub] . sequence [j])
              {
               hp = NULL;
               hp_len = 0;
               hp_ch = 'x';
              }
            else
              {
               hp = Frag [sub] . vote + j;
               hp_ch = Frag [sub] . sequence [j];
               hp_len = 0;
              }
           if (dp -> a_lo < j && insert_ct == 0)
             Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
           j ++;
           insert_ct = 0;
           break;
         case 2 :    // substitute
           if (Char_Matches (hp_ch, dp -> de [k] . ch))
             {
              // allow overlap of 1 for homopolymer runs
              // shouldn't be necessary if alignments are done correctly
              hp_len ++;
              do_subst = FALSE;
             }
           else
             do_subst = TRUE;
           if (hp != NULL)
             Add_Homopoly_Vote (hp, hp_len);
           if (hp_ch == Frag [sub] . sequence [j])
              {
               hp = NULL;
               hp_len = 0;
               hp_ch = 'x';
              }
            else
              {
               hp = Frag [sub] . vote + j;
               hp_ch = Frag [sub] . sequence [j];
               hp_len = 0;
               if (do_subst)
                 Cast_Substitution_Vote (hp, dp -> de [k] . ch);
              }
           if (dp -> a_lo < j && insert_ct == 0)
             Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
           j ++;
           insert_ct = 0;
           break;
         case 3 :    // noop
           // should be end of alignment
           // cast vote at end also--should never need to lengthen, but
           // there may be evidence to shorten
           if (hp != NULL)
             Add_Homopoly_Vote (hp, hp_len);
           hp = NULL;    // following are just in case alignment doesn't end here
           hp_len = 0;
           hp_ch = 'x';
           insert_ct = 0;
           break;
        }
     }

   if (hp != NULL)
     Add_Homopoly_Vote (hp, hp_len);

   return;
  }



static void  Set_Insert_Sizes
  (short unsigned * insert_size, const Sequence_Diff_t * dp)

// Set values in  insert_size  to be at least large enough to accommodate
// insertitions in  dp .

  {
   int  j, k, i_ct;

   j = dp -> a_lo;
   i_ct = 0;

   for (k = 0; k < dp -> diff_len; k ++)
     {
      if (0 < dp -> de [k] . len)
        {
         j += dp -> de [k] . len;
         i_ct = 0;
        }
      switch (dp -> de [k] . action)
        {
         case 0 :    // insert
           if (j == dp -> a_lo)
             {
              fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
              fprintf (stderr, "Insertion at beginning of alignment\n");
              exit (-1);
             }
           if (++ i_ct > insert_size [j - 1])
             insert_size [j - 1] = i_ct;
           break;
         case 1 :    // delete
         case 2 :    // substitute
           j ++;
           i_ct = 0;
           break;
         case 3 :    // noop
           // do nothing--should be end of alignment
           break;
        }
     }

   return;
  }



static void  Set_New_Homopoly_Votes
  (New_Vote_t * vote, const char * seq, int len, const Sequence_Diff_t * dp)

// Set entries in  vote  for sequence  seq  of length  len  for differences
// in  dp  that correspond to a homopolymer-type read.

  {
   New_Vote_t  * hp = NULL;     // pointer to start of current homopoly run
   Diff_Entry_t  * first_de, * last_de;
   char  hp_ch = 'x';
   int  skip_last;
   int  first_j, last_j;
   int  hp_len = 0;      // length of current homopoly run
   int  j, k, m;

   j = dp -> a_lo;

   if (dp -> diff_len > 0)
     {
      first_de = dp -> de;
      last_de = dp -> de + dp -> diff_len - 1;
     }
   else
     first_de = last_de = NULL;
   // if alignment starts in middle don't allow votes for first hp run
   // because it could be truncated because the fragment boundary
   // split the run
   if (0 < j && first_de != NULL && first_de -> len > 0)
     {
      for (k = 1; k < first_de -> len && seq [j + k] == seq [j]; k ++)
        ;
      first_j = j + k;
     }
   else
     first_j = j;

   // similarly, if the alignment ends before the end of seq, don't
   // allow votes for the last matching hp run
   if (dp -> a_hi < len && last_de != NULL
          && last_de -> len > 0 && last_de -> action == 3)
     {
      char  ch = seq [dp -> a_hi - 1];
      for (k = 2; k <= last_de -> len && seq [dp -> a_hi - k] == ch ; k ++)
        ;
      last_j = dp -> a_hi - k;
      skip_last = TRUE;
     }
   else
     {
      last_j = dp -> a_hi;
      skip_last = FALSE;
     }
   

   for (k = 0; k < dp -> diff_len; k ++)
     {
      for (m = 0; m < dp -> de [k] . len; m ++)
        {
         if (first_j <= j && j <= last_j)
           Cast_New_Vote_Char (vote + j, seq [j]);
         if (seq [j] == hp_ch)
           hp_len ++;
         else
           {
            if (hp != NULL && first_j < j)
              {
               hp -> homopoly_ct ++;
               hp -> homopoly_sum += hp_len;
              }
            hp_len = 1;
            hp = vote + j;
            hp_ch = seq [j];
           }
         j ++;
        }

      switch (dp -> de [k] . action)
        {
         case 0 :    // insert
           if (Char_Matches (hp_ch, dp -> de [k] . ch))
              hp_len ++;
           else
             {
              if (hp != NULL && first_j < j)
                {
                 hp -> homopoly_ct ++;
                 hp -> homopoly_sum += hp_len;
                }
              hp = NULL;
              hp_len = 0;
              hp_ch = 'x';
             }
           break;
         case 1 :    // delete
           if (hp != NULL && first_j < j)
                {
                 hp -> homopoly_ct ++;
                 hp -> homopoly_sum += hp_len;
                }
           if (hp_ch == seq [j])
              {
               hp = NULL;
               hp_len = 0;
               hp_ch = 'x';
              }
            else
              {
               hp = vote + j;
               hp_ch = seq [j];
               hp_len = 0;
              }
           Cast_New_Vote_Char (vote + j, '-');
           j ++;
           break;
         case 2 :    // substitute
           if (Char_Matches (hp_ch, dp -> de [k] . ch))
             {
              // allow overlap of 1 for homopolymer runs
              // shouldn't be necessary if alignments are done correctly
              hp_len ++;
             }
           if (hp != NULL && first_j < j)
                {
                 hp -> homopoly_ct ++;
                 hp -> homopoly_sum += hp_len;
                }
           if (hp_ch == seq [j])
              {
               hp = NULL;
               hp_len = 0;
               hp_ch = 'x';
              }
            else
              {
               hp = vote + j;
               hp_ch = seq [j];
               hp_len = 0;
              }
           Cast_New_Vote_Code (vote + j, dp -> de [k] . ch);
           j ++;
           break;
         case 3 :    // noop
           // should be end of alignment
           // cast vote at end also--requires homopoly-type alignment
           // or else may unduly shorten run
           if (hp != NULL && first_j < j && ! skip_last)
              {
               hp -> homopoly_ct ++;
               hp -> homopoly_sum += hp_len;
              }
           hp = NULL;    // following are just in case alignment doesn't end here
           hp_len = 0;
           hp_ch = 'x';
           break;
        }
     }

   if (hp != NULL)
      {
       hp -> homopoly_ct ++;
       hp -> homopoly_sum += hp_len;
      }

   return;
  }



static void  Set_New_Self_Homopoly_Votes
  (New_Vote_t * vote, const char * seq, int len)

// Set homopolymer-type votes in  vote  based on the sequence  seq
// of length  len .

  {
   New_Vote_t  * hp;   // keeps track of first base of homopoly run
   char  hp_ch = 'x';
   int  hp_len = 0;      // length of current homopoly run
   int  j;

   hp = NULL;

   for (j = 0; j < len; j ++)
     {
      if (seq [j] == hp_ch)
        hp_len ++;
      else
        {
         if (hp != NULL)
           {
            hp -> homopoly_ct ++;
            hp -> homopoly_sum += hp_len;
           }
         hp_len = 1;
         hp = vote + j;
         hp_ch = seq [j];
        }
      Cast_New_Vote_Char (vote + j, seq [j]);
     }

   if (hp != NULL)
     {
      hp -> homopoly_ct ++;
      hp -> homopoly_sum += hp_len;
     }

   return;
  }



static void  Set_New_Standard_Votes
  (New_Vote_t * vote, int len, const Sequence_Diff_t * dp)

// Set entries in  vote  of length  len  for differences
// in  dp  that correspond to a conventional-type (non-homopolymer) read.

  {
   //**ALD  not done yet

   return;
  }



static void  Set_Self_Homopoly_Votes
  (int sub, int frag_len)

// Set homopolymer-type votes in  Frag [sub]  base on the sequence itself
//  frag_len  is the length of the sequence.

  {
   Vote_Tally_t  * hp;   // keeps track of first base of homopoly run
   char  hp_ch = 'x';
   int  hp_len = 0;      // length of current homopoly run
   int  j;

   hp = NULL;

   for (j = 0; j < frag_len; j ++)
     {
      if (Frag [sub] . sequence [j] == hp_ch)
        hp_len ++;
      else
        {
         if (hp != NULL)
           Add_Homopoly_Vote (hp, hp_len);
         hp_len = 1;
         hp = Frag [sub] . vote + j;
         hp_ch = Frag [sub] . sequence [j];
        }
      if (0 < j)
        Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
     }

   if (hp != NULL)
     Add_Homopoly_Vote (hp, hp_len);

   return;
  }



static void  Set_Self_Votes
  (int sub, int frag_len)

// Set standard-type votes in  Frag [sub]  base on the sequence itself
//  frag_len  is the length of the sequence.

  {
   int  j;

   for (j = 0; j < frag_len; j ++)
     {
      if (0 < j)
        Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
      Cast_Confirmation_Vote (Frag [sub] . vote + j);
     }

   return;
  }



static void  Set_Votes_From_Diffs
  (int sub, Sequence_Diff_t * dp)

// Set votes in  Frag [sub]  from the alignment entries in  dp .

  {
   int  insert_ct = 0;
   int  j, k, m;

   j = dp -> a_lo;

   for (k = 0; k < dp -> diff_len; k ++)
     {
      for (m = 0; m < dp -> de [k] . len; m ++)
        {
         if (dp -> a_lo < j && insert_ct == 0)
           Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
         Cast_Confirmation_Vote (Frag [sub] . vote + j);
         j ++;
         insert_ct = 0;
        }
      switch (dp -> de [k] . action)
        {
         case 0 :    // insert
           // if consecutive inserts only cast vote for first
           if (insert_ct ++ == 0 && dp -> a_lo < j)
             Cast_Insert_Vote (Frag [sub] . vote + j - 1, dp -> de [k] . ch);
           break;
         case 1 :    // delete
           if (dp -> a_lo < j && insert_ct == 0)
             Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
           Cast_Delete_Vote (Frag [sub] . vote + j);
           j ++;
           insert_ct = 0;
           break;
         case 2 :    // substitute
           if (dp -> a_lo < j && insert_ct == 0)
             Cast_No_Insert_Vote (Frag [sub] . vote + j - 1);
           Cast_Substitution_Vote (Frag [sub] . vote + j, dp -> de [k] . ch);
           j ++;
           insert_ct = 0;
           break;
         case 3 :    // noop
           // do nothing--should be end of alignment
           insert_ct = 0;  // just in case
           break;
        }
     }

   return;
  }



static void  Show_Corrections
  (FILE * fp, const char * seq, const char * corr, int len)

// Display to  fp  the contents of  seq  with  corr  underneath.
// Both have length  len .

  {
   int  i, lo, hi;

   for (lo = 0; lo < len; lo += DISPLAY_WIDTH)
     {
      fputc ('\n', fp);
      hi = lo + DISPLAY_WIDTH;
      if (len < hi)
        hi = len;

      fprintf (fp, "%7s:  ", "ref");
      for (i = lo; i < hi; i ++)
        fputc (seq [i], fp);
      fputc ('\n', fp);

      fprintf (fp, "%7s:  ", "cor");
      for (i = lo; i < hi; i ++)
        fputc (corr [i], fp);
      fputc ('\n', fp);
     }

   return;
  }



static void  Show_Edit_Array
  (int * * ea, int errs)

// Display the values of  ea  in rows  0 .. errs .

  {
   int  i, j;

   for (i = 0; i <= errs; i ++)
     {
      printf ("%2d: ", i);
      for (j = -i; j <= i; j ++)
         printf (" %3d", ea [i] [j]);
      putchar ('\n');
     }

   return;
  }



static void  Show_Frag_Votes
  (FILE * fp, int sub)

// Display to  fp  the votes values of  Frag [sub]

  {
   Frag_Info_t  * f = Frag + sub;
   int  j;

   printf ("\n>%d\n", Lo_Frag_IID + sub);
   for  (j = 0;  Frag [sub] . sequence [j] != '\0';  j ++)
     {
      Vote_Tally_t  * v = f -> vote + j;

      printf ("%3d: %c  %3d  %3d | %3d %3d %3d %3d | %3d %3d %3d %3d %3d"
           " | %3d %5.2f\n",
           j,
           (j >= f -> clear_len ?
               toupper (f -> sequence [j]) : f -> sequence [j]),
           v -> confirmed,
           v -> deletes,
           v -> a_subst,
           v -> c_subst,
           v -> g_subst,
           v -> t_subst,
           v -> no_insert,
           v -> a_insert,
           v -> c_insert,
           v -> g_insert,
           v -> t_insert,
           v -> homopoly_ct,
           (v -> homopoly_ct == 0 ? 0.0
                : (1.0 * v -> homopoly_sum) / v -> homopoly_ct));
     }
   
   return;
  }


static void  Show_New_Votes
  (FILE * fp, const char * seq, const New_Vote_t * vp, int n)

// Display the  n  vote values in  vp  for sequence  seq  to  fp .

  {
   int  i;

   for (i = 0; i < n; i ++)
     fprintf (fp, "%3d: %c  %4d %4d %4d %4d %4d  %4d %5.1f  %4d %5.1f\n",
          i, seq [i],
          vp [i] . a_ct, vp [i] . c_ct, vp [i] . g_ct, vp [i] . t_ct,
          vp [i] . gap_ct, vp [i] . homopoly_ct,
          (vp [i] . homopoly_ct == 0 ? 0.0
             : vp [i] . homopoly_sum / vp [i] . homopoly_ct),
          vp [i] . non_hp_ct,
          (vp [i] . non_hp_ct == 0 ? 0.0
             : vp [i] . non_hp_sum / vp [i] . non_hp_ct));
  }



static void  Show_Votes
  (FILE * fp)

// Display the vote values for all sequences in  Frag  to  fp .

  {
   int  i;

   for  (i = 0;  i < Num_Frags;  i ++)
     Show_Frag_Votes (fp, i);
   
   return;
  }



static void  Stream_Old_Frags
    (void)

//  Read old fragments in  gkpStore  and choose the ones that
//  have overlaps with fragments in  Frag .  Recompute the
//  overlaps and record the vote information about changes to
//  make (or not) to fragments in  Frag .

  {
   Thread_Work_Area_t  wa;
   fragRecord  * frag_read;
   char  * seq_ptr;
   char  seq_buff [AS_READ_MAX_LEN + 1];
   char  rev_seq [AS_READ_MAX_LEN + 1] = "acgt";
   unsigned  clear_start, clear_end, seq_len;
   int32  lo_frag, hi_frag;
   int  next_olap;
   int  i, j;

   Init_Thread_Work_Area (& wa, 0);
   frag_read = new_fragRecord ();

   Frag_Stream = openFragStream (gkpStore, FRAG_S_SEQ);

   lo_frag = Olap [0] . b_iid;
   hi_frag = Olap [Num_Olaps - 1] . b_iid;

   resetFragStream (Frag_Stream, lo_frag, hi_frag);
   
   next_olap = 0;
   for (i = 0; nextFragStream (Frag_Stream, frag_read)
        && next_olap < Num_Olaps; i ++)
     {
      int32  rev_id;
      uint32  frag_iid;
      int  raw_len, result, shredded, is_homopoly;

      frag_iid = (uint32) getFragRecordIID (frag_read);
      if  (frag_iid < Olap [next_olap] . b_iid)
          continue;

      if (getFragRecordIsDeleted (frag_read))
          continue;

      shredded = FALSE;
        // Used in Process_Olap to ignore overlaps between two "shredded" reads
        // Perhaps should check for external reads now

      clear_start = getFragRecordClearRegionBegin (frag_read, AS_READ_CLEAR_OBT);
      clear_end = getFragRecordClearRegionEnd (frag_read, AS_READ_CLEAR_OBT);
      raw_len = getFragRecordSequenceLength (frag_read);
      seq_ptr = getFragRecordSequence (frag_read);
      is_homopoly = Is_Homopoly_Type (frag_read, gkpStore);

      if (AS_READ_MAX_LEN < clear_end - clear_start)
        {
         fprintf (stderr, "ERROR:  line %d  file %s\n", __LINE__, __FILE__);
         fprintf (stderr, "Read %u is too long:  %d bp; max is %d\n",
              frag_iid, clear_end - clear_start, AS_READ_MAX_LEN);
         exit (-1);
        }

      // Make sure that we have a valid lowercase sequence string
      for (j = clear_start; j < clear_end; j ++)
        seq_buff [j - clear_start] = Filter (seq_ptr [j]);
      seq_len = clear_end - clear_start;
      seq_buff [seq_len] = '\0';

      rev_id = -1;
      while  (next_olap < Num_Olaps
                && Olap [next_olap] . b_iid == frag_iid)
        {
         if (Offsets_WRT_Raw)
           {
            if (Olap [next_olap] . orient == NORMAL)
               Olap [next_olap] . b_hang -= clear_start;
            else
               Olap [next_olap] . b_hang -= (raw_len - clear_end);
           }
         Process_Olap (Olap + next_olap, seq_buff, seq_len,
              rev_seq, & rev_id, shredded, is_homopoly, & wa);
         next_olap ++;
        }
     }

   del_fragRecord (frag_read);
   closeFragStream (Frag_Stream);

   Failed_Olaps = wa . failed_olaps;

   return;
  }



void *  Threaded_Process_Stream
    (void * ptr)

//  Process all old fragments in  Internal_gkpStore .  Only
//  do overlaps/corrections with fragments where
//    frag_iid % Num_PThreads == thread_id

  {
   Thread_Work_Area_t  * wa = (Thread_Work_Area_t *) ptr;
   int  olap_ct;
   int  i;

   olap_ct = 0;

   for  (i = 0;  i < wa -> frag_list -> ct;  i ++)
     {
      int32  skip_id = -1;

      while  (wa -> frag_list -> entry [i] . id > Olap [wa -> next_olap] . b_iid)
        {
         if  (Olap [wa -> next_olap] . b_iid != skip_id)
             {
              fprintf (stderr, "SKIP:  b_iid = %d\n",
                       Olap [wa -> next_olap] . b_iid);
              skip_id = Olap [wa -> next_olap] . b_iid;
             }
         wa -> next_olap ++;
        }
      if  (wa -> frag_list -> entry [i] . id != Olap [wa -> next_olap] . b_iid)
          {
           fprintf (stderr, "ERROR:  Lists don't match\n");
           fprintf (stderr, "frag_list iid = %d  next_olap = %d  i = %d\n",
                    wa -> frag_list -> entry [i] . id,
                    Olap [wa -> next_olap] . b_iid, i);
           exit (EXIT_FAILURE);
          }

      wa -> rev_id = -1;
      while  (wa -> next_olap < Num_Olaps
                && Olap [wa -> next_olap] . b_iid == wa -> frag_list -> entry [i] . id)
        {
         if (Olap [wa -> next_olap] . a_iid % Num_PThreads == wa -> thread_id)
           {
            int  b_len;

            b_len = wa -> frag_list -> entry [i + 1] . start
                 - wa -> frag_list -> entry [i] . start - 1;

            if (Offsets_WRT_Raw)
              {
               if (Olap [wa -> next_olap] . orient == NORMAL)
                  Olap [wa -> next_olap] . b_hang
                       -= wa -> frag_list -> entry [i] . trim_5p;
               else
                  Olap [wa -> next_olap] . b_hang
                       -= wa -> frag_list -> entry [i] . trim_3p;
              }
            Process_Olap (Olap + wa -> next_olap,
                 wa -> frag_list -> buffer + wa -> frag_list -> entry [i] . start,
                 b_len, wa -> rev_seq, & (wa -> rev_id),
                 wa -> frag_list -> entry [i] . shredded,
                 wa -> frag_list -> entry [i] . is_homopoly_type, wa);
            olap_ct ++;
           }
         wa -> next_olap ++;
        }
     }

pthread_mutex_lock (& Print_Mutex);
Now = time (NULL);
fprintf (stderr, "Thread %d processed %d olaps at %s",
         wa -> thread_id, olap_ct, ctime (& Now));
pthread_mutex_unlock (& Print_Mutex);

//   if  (wa -> thread_id > 0)
       pthread_exit (ptr);

   return  ptr;
  }



static void  Threaded_Stream_Old_Frags
    (void)

//  Read old fragments in  gkpStore  that have overlaps with
//  fragments in  Frag .  Read a batch at a time and process them
//  with multiple pthreads.  Each thread processes all the old fragments
//  but only changes entries in  Frag  that correspond to its thread
//  ID.  Recomputes the overlaps and records the vote information about
//  changes to make (or not) to fragments in  Frag .

  {
   pthread_attr_t  attr;
   pthread_t  * thread_id;
   Frag_List_t  frag_list_1, frag_list_2;
   Frag_List_t  * curr_frag_list, * next_frag_list, * save_frag_list;
   Thread_Work_Area_t  * thread_wa;
   int  next_olap, save_olap, status;
   int32  first_frag, last_frag, lo_frag, hi_frag;
   int  i;

   fprintf (stderr, "### Using %d pthreads (new version)\n", Num_PThreads);

   pthread_mutex_init (& Print_Mutex, NULL);
   pthread_attr_init (& attr);
   pthread_attr_setstacksize (& attr, THREAD_STACKSIZE);
   thread_id = (pthread_t *) safe_calloc
                   (Num_PThreads, sizeof (pthread_t));
   thread_wa = (Thread_Work_Area_t *) safe_malloc
                   (Num_PThreads * sizeof (Thread_Work_Area_t));

   for  (i = 0;  i < Num_PThreads;  i ++)
     Init_Thread_Work_Area (thread_wa + i, i);
   Init_Frag_List (& frag_list_1);
   Init_Frag_List (& frag_list_2);

   first_frag = Olap [0] . b_iid;
   last_frag = Olap [Num_Olaps - 1] . b_iid;
   assert (first_frag <= last_frag);

   lo_frag = first_frag;
   hi_frag = lo_frag + FRAGS_PER_BATCH - 1;
   if  (hi_frag > last_frag)
       hi_frag = last_frag;
   next_olap = 0;

#ifdef USE_STORE_DIRECTLY_STREAM
  Internal_gkpStore = openGateKeeperStore (gkpStore_Path, FALSE);
  assert (Internal_gkpStore != NULL);
#else
   Internal_gkpStore
       = loadFragStorePartial (gkpStore_Path, lo_frag, hi_frag);
#endif

   curr_frag_list = & frag_list_1;
   next_frag_list = & frag_list_2;
   save_olap = next_olap;

   Extract_Needed_Frags (Internal_gkpStore, lo_frag, hi_frag,
                         curr_frag_list, & next_olap);

#ifndef USE_STORE_DIRECTLY_STREAM
   closeGateKeeperStore (Internal_gkpStore);
#endif

   while  (lo_frag <= last_frag)
     {
      // Process fragments in  curr_frag_list  in background
      for  (i = 0;  i < Num_PThreads;  i ++)
        {
         thread_wa [i] . lo_frag = lo_frag;
         thread_wa [i] . hi_frag = hi_frag;
         thread_wa [i] . next_olap = save_olap;
         thread_wa [i] . frag_list = curr_frag_list;
         status = pthread_create
                      (thread_id + i, & attr, Threaded_Process_Stream,
                       thread_wa + i);
         if  (status != 0)
             {
              fprintf (stderr, "pthread_create error at line %d:  %s\n",
                       __LINE__, strerror (status));
              exit (-3);
             }
        }

      // Read next batch of fragments
      lo_frag = hi_frag + 1;
      if  (lo_frag <= last_frag)
          {
           hi_frag = lo_frag + FRAGS_PER_BATCH - 1;
           if  (hi_frag > last_frag)
               hi_frag = last_frag;

#ifndef USE_STORE_DIRECTLY_STREAM
           Internal_gkpStore
               = loadFragStorePartial (gkpStore_Path, lo_frag, hi_frag);
#endif

           save_olap = next_olap;

           Extract_Needed_Frags (Internal_gkpStore, lo_frag, hi_frag,
                                 next_frag_list, & next_olap);

#ifndef USE_STORE_DIRECTLY_STREAM
           closeGateKeeperStore (Internal_gkpStore);
#endif
          }

      // Wait for background processing to finish
      for  (i = 0;  i < Num_PThreads;  i ++)
        {
         void  * ptr;

         status = pthread_join (thread_id [i], & ptr);
         if  (status != 0)
             {
              fprintf (stderr, "pthread_join error at line %d:  %s\n",
                       __LINE__, strerror (status));
              exit (-3);
             }
        }

      save_frag_list = curr_frag_list;
      curr_frag_list = next_frag_list;
      next_frag_list = save_frag_list;
     }
   
#ifdef USE_STORE_DIRECTLY_STREAM
   closeGateKeeperStore (Internal_gkpStore);
#endif

   for (i = 0; i < Num_PThreads; i ++)
     Failed_Olaps += thread_wa [i] . failed_olaps;

   return;
  }



static void  Tidy_Up
  (void)

//  Close any open files

  {
   switch (OVL_Output_Type)
     {
      case TEXT_FILE :
        fclose (OVL_Output_fp);
        break;
      case BINARY_FILE :
        AS_OVS_closeBinaryOverlapFile (Binary_OVL_Output_fp);
        break;
     }

   return;
  }



static void  Usage
    (char * command)

//  Print to stderr description of options and command line for
//  this program.   command  is the command that was used to
//  invoke it.

  {
   char  * p, * q = NULL;

   for (p = command; * p != '\0'; p ++)
     if (* p == '/' || * p == '\\')
       q = p + 1;
   if (q == NULL)
     q = command;
   
   fprintf (stderr,
        "USAGE:  %s [-behp] [-d DegrThresh] [-k ConfLen] [-x ExcludeLen]\n"
        "     [-F OlapFile|-S OlapStore] [-c CorrectFile] [-o OlapOutput]\n"
        "     [-t NumPThreads] [-v VerboseLevel] [-V Vote_Qualify_Len]\n"
        "     <FragStore> <lo> <hi>\n"
        "\n"
        "Extract from a store exact-match seeds between pairs of\n"
        "reads and use them to determine if the pair actually overlaps.\n"
        "These overlaps can then be used to correct errors in reads based\n"
        "on the alignment of all overlapping reads to a given read.\n"
        "Fragments come from <FragStore>.  <lo> and <hi> specify\n"
        "the range of fragments to modify.\n"
        "\n"
        "Options:\n"
        "-a      Asymmetric overlaps, i.e., only output overlaps with\n"
        "        a_iid < b_iid\n"
        "-b      Output binary overlap messages\n"
        "-c <f>  Output corrections to file <f>\n"
        "-d <n>  Set keep flag in correction record on end of frags with less\n"
        "        than <n> olaps\n"
        "-e      Extend fragments beyond 3' clear range\n"
        "-F <f>  Read seeds from sorted file <f>.  Format is:\n"
        "        <a_iid> <b_iid> [f\\r] <a_pos> <b_pos> <ct> <len>\n"
        "        additional entries on line are ignored.\n"
        "        Entries are not reversed, i.e., a/b seed does NOT\n"
        "        automatically generate the b/a seed, too\n"
        "-G      Do partial overlaps (i.e., local alignments)\n"
        "-h      Print this message\n"
        "-k <n>  Prevent correction when have an exact match of <n> bases\n"
        "-o <f>  Send overlaps to file or store <f>\n"
        "-p      Don't use haplotype counts to correct\n"
        "-S <f>  Read seeds from binary overlap store <f>\n"
        "-t <n>  Use <n> p-threads\n"
        "-v <n>  Set verbose level to <n>; higher is more debugging output\n"
        "-V <n>  Require <n> exact match bases around an error (combined count\n"
        "        on both sides) to vote to change a base\n"
        "-x <n>  Don't prevent correction on first and last <n> bp of exact match\n"
        "        regions (whose length is set by -k option).\n"
        "-y <x>  Allow max error rate of <x> in alignments (e.g., 0.03 for 3%% error)\n"
        "        Value cannot exceed  %.3f\n"
        "-z      Do NOT try to correct read errors\n",
        q, MAX_ERROR_RATE);

   return;
  }


static int  Votes_For
  (char ch, const New_Vote_t * vp)

// Return the number of votes corresponding to character  ch  in  vp

  {
   switch (tolower (ch))
     {
      case 'a' :
        return  vp -> a_ct;
      case 'c' :
        return  vp -> c_ct;
      case 'g' :
        return  vp -> g_ct;
      case 't' :
        return  vp -> t_ct;
      case '-' :
        return  vp -> gap_ct;
     }

   return  0;
  }



