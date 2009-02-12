/*
 
 UIKBStandardKeyboard.m ... Standard Keyboard Definitions for iKeyEx.
 
 Copyright (c) 2009, KennyTM~
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without modification,
 are permitted provided that the following conditions are met:
 
 * Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright notice, 
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
 * Neither the name of the KennyTM~ nor the names of its contributors may be
   used to endorse or promote products derived from this software without
   specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 
 */

#import <iKeyEx/UIKBStandardKeyboard.h>
#import <iKeyEx/common.h>
#import <iKeyEx/ImageLoader.h>
#import <iKeyEx/UIKBKeyDefinition.h>
#import <UIKit2/UIKeyboardImpl.h>
#import <UIKit2/Constants.h>
#import <UIKit/UIGraphics.h>
#import <GraphicsUtilities.h>
#include <stdlib.h>
#include <string.h>

#define DefaultHeight(l)  ((l)?38:44)
#define KeyboardHeight(l) ((l)?162:216)
#define KeyboardWidth(l)  ((l)?480:320)
#define DefaultVerticalOffset 2
#define MaximumVerticalSpacing 10
#define Ratio 47.f/32.f
#define LastRowHorizontalOffset 48
#define LandscapeMargin 5
#define Padding 8
#define ShiftDeleteDefaultWidth 42
static const CGSize UIKBKeyPopupSize = {44, 50};

NSString* referedKeyOf(NSString* key) {
	if ([key isEqualToString:@"shiftedTexts"])
		return @"texts";
	else if ([key hasPrefix:@"shifted"])
		return @"shiftedTexts";
	else
		return @"texts";
}

#define RecursionLimit 8
NSArray* fetchTextRow(NSString* curKey, NSUInteger row, NSDictionary* restrict sublayout, NSDictionary* restrict layoutDict, NSUInteger depth) {
	// too deep. break out by returning nothing.
	if (depth >= RecursionLimit)
		return nil;
	
	NSArray* allRows = [sublayout objectForKey:curKey];
	if ([allRows count] <= row) {
		if (![curKey isEqualToString:@"text"])
			return fetchTextRow(referedKeyOf(curKey), row, sublayout, layoutDict, depth+1);
		else
			return nil;
	}
	NSArray* rowContent = [allRows objectAtIndex:row];
	
	if ([rowContent isKindOfClass:[NSArray class]])
		// really an array.
		return [rowContent retain];
	
	else if ([rowContent isKindOfClass:[NSString class]]) {
		if ([(NSString*)rowContent length] > 0) {
			NSString* firstChar = [(NSString*)rowContent substringToIndex:1];
			NSString* rest = [(NSString*)rowContent substringFromIndex:1];
			if (![firstChar isEqualToString:@"="]) {
				// concat'd string. split it and return
				return [[rest componentsSeparatedByString:firstChar] retain];
			} else {
				// refered object.
				// test if the row index is explicitly stated.
				NSRange leftBrac = [rest rangeOfString:@"["];
				NSString* refSublayoutKey = rest;
				NSUInteger specificRow = row;
				if (leftBrac.location != NSNotFound) {
					specificRow = [[rest substringFromIndex:leftBrac.length+leftBrac.location] integerValue];
					refSublayoutKey = [rest substringToIndex:leftBrac.location];
				}
				NSDictionary* refSublayout = [layoutDict objectForKey:refSublayoutKey];
				return fetchTextRow((refSublayout == sublayout ? referedKeyOf(curKey) : curKey),
									specificRow, refSublayout, layoutDict, depth+1);
			}
		}
	}
	
	
	return fetchTextRow(referedKeyOf(curKey), row, sublayout, layoutDict, depth+1);
}

@interface UIKBStandardKeyboard ()
-(UIImage*)imageWithShift:(BOOL)shift;
-(UIImage*)fgImageWithShift:(BOOL)shift;
@end


@implementation UIKBStandardKeyboard
-(void)dealloc {
	for (NSUInteger i = 0; i < rows; ++ i) {
		[texts[i] release];
		[shiftedTexts[i] release];
		[labels[i] release];
		[shiftedLabels[i] release];
		[popups[i] release];
		[shiftedPopups[i] release];
	}
	free(texts);
	free(shiftedTexts);
	free(labels);
	free(shiftedLabels);
	free(popups);
	free(shiftedPopups);
	free(counts);
	free(lefts);
	free(rights);
	free(widths);
	free(horizontalSpacings);
	[super dealloc];
}

-(id)initWithPlist:(NSDictionary*)layoutDict name:(NSString*)name landscape:(BOOL)landsc appearance:(UIKeyboardAppearance)appr {
	if ((self = [super init])) {
		NSDictionary* sublayout = [layoutDict objectForKey:name];
		
		// get number of rows
		NSNumber* rowCount = [sublayout objectForKey:@"rows"];
		if (rowCount != nil) {
			rows = [rowCount integerValue];
			if (rows == 1)
				rows = 2;
			else if (rows == 0)
				rows = 4;
		} else
			rows = 4;
		
		// initialize & calloc the arrays
		texts         = calloc(rows, sizeof(NSArray*));
		shiftedTexts  = calloc(rows, sizeof(NSArray*));
		labels        = calloc(rows, sizeof(NSArray*));
		shiftedLabels = calloc(rows, sizeof(NSArray*));
		popups        = calloc(rows, sizeof(NSArray*));
		shiftedPopups = calloc(rows, sizeof(NSArray*));
		counts        = calloc(rows, sizeof(NSUInteger)); 
		lefts         = calloc(rows, sizeof(CGFloat)); 
		rights        = calloc(rows, sizeof(CGFloat)); 
		widths        = calloc(rows, sizeof(CGFloat)); 
		horizontalSpacings = calloc(rows, sizeof(CGFloat));
		
		// fill in rest of variables.
		landscape = landsc;
		keyboardAppearance = appr;
		NSUInteger keyboardWidth = KeyboardWidth(landscape);
		keyboardSize = CGSizeMake(keyboardWidth, KeyboardHeight(landscape));
		
		// compute height & vertical spacings of each key.
		defaultHeight = DefaultHeight(landscape);
		CGFloat availableHeight = KeyboardHeight(landscape) - defaultHeight - DefaultVerticalOffset;
		if (availableHeight >= defaultHeight*(rows-1)) {
			if (availableHeight >= MaximumVerticalSpacing*rows + defaultHeight*(rows-1)) {
				verticalSpacing = MaximumVerticalSpacing;
				keyHeight = (availableHeight - MaximumVerticalSpacing*rows)/(rows-1);
			} else {
				verticalSpacing = (availableHeight-defaultHeight*(rows-1))/rows;
				keyHeight = defaultHeight;
			}
		} else {
			verticalSpacing = 0;
			keyHeight = availableHeight/(rows-1);
		}	
		verticalOffset = 2+availableHeight-(verticalSpacing+keyHeight)*(rows-1);
		
		// fetch the arrangement.
		NSObject* arrangement = [sublayout objectForKey:@"arrangement"];
		if ([arrangement isKindOfClass:[NSArray class]]) {
			NSUInteger i = 0;
			for (NSNumber* n in (NSArray*)arrangement) {
				counts[i] = [n integerValue];
				++ i;
				if (i >= rows)
					break;
			}
		} else {
			// legacy support for strings...
			NSUInteger r0 = 10, r1 = 9, r2 = 7, r3 = 0;
			if ([arrangement isKindOfClass:[NSString class]]) {
				NSString* theStr = [(NSString*)arrangement lowercaseString];
				if ([theStr hasPrefix:@"withurlrow4|"]) {
					r3 = 3;
					// gcc should be able to remove the strlen during optimization.
					theStr = [theStr substringFromIndex:strlen("withurlrow4|")];
				} else if ([theStr hasSuffix:@"|withurlrow4"]) {
					r3 = 3;
					theStr = [theStr substringToIndex:[theStr length]-strlen("|withurlrow4")];
				}
				
				if ([theStr isEqualToString:@"withurlrow4"]) {
					r3 = 3;
				} else if ([theStr isEqualToString:@"azerty"]) {
					r0 = 10; r1 = 10; r2 = 6;
				} else if ([theStr isEqualToString:@"russian"]) {
					r0 = 11; r1 = 11; r2 = 9;
				} else if ([theStr isEqualToString:@"alt"]) {
					r0 = 10; r1 = 10; r2 = 5;
				} else if ([theStr isEqualToString:@"urlalt"]) {
					r0 = 10; r1 = 6; r2 = 4;
				} else if ([theStr isEqualToString:@"japaneseqwerty"]) {
					r0 = 10; r1 = 10; r2 = 7;
				}
			}
			
			counts[rows-1] = r3;
			if (rows >= 2) counts[rows-2] = r2;
			if (rows > 3)
				counts[rows-3] = r1;
			else if (rows == 3)
				counts[1] = r1;
			counts[0] = r0;
			for (NSUInteger i = 1; i <= rows-4; ++i)
				counts[i] = r0;
		}
		for (NSUInteger i = 0; i < rows; ++ i)
			totalCount += counts[i];
		
		// fetch indentation.
		NSObject* indentation = [sublayout objectForKey:@"rowIndentation"];
		if ([indentation isKindOfClass:[NSArray class]]) {
			NSUInteger i = 0;
			for (NSNumber* n in (NSArray*)indentation) {
				if ([n isKindOfClass:[NSArray class]]) {
					lefts[i] = [[(NSArray*)n objectAtIndex:0] floatValue];
					rights[i] = [[(NSArray*)n lastObject] floatValue];
				} else {
					lefts[i] = rights[i] = [n floatValue];
				}
				++ i;
				if (i >= rows)
					break;
			}
		} else {
			// legacy support for strings...
			CGFloat r0 = 0, r1 = 16, r2 = 48, r3 = 80;
			if ([indentation isKindOfClass:[NSString class]]) {
				NSString* theStr = [(NSString*)indentation lowercaseString];
				if ([theStr isEqualToString:@"azerty"]) {
					r1 = 0; r2 = 64;
				} else if ([theStr isEqualToString:@"russian"]) {
					r1 = 0; r2 = 29;
				} else if ([theStr isEqualToString:@"alt"]) {
					r1 = 0;
				} else if ([theStr isEqualToString:@"tightestdefault"]) {
					r1 = 0; r2 = 42;
				}
			}
			
			lefts[rows-1] = rights[rows-1] = r3;
			if (rows >= 2) lefts[rows-2] = rights[rows-2] = r2;
			if (rows > 3)
				lefts[rows-3] = rights[rows-3] = r1;
			else if (rows == 3)
				lefts[1] = rights[1] = r1;
			lefts[0] = rights[0] = r0;
			if (r0 > 0)
				for (NSUInteger i = 1; i <= rows-4; ++i)
					lefts[i] = rights[i] = r0;
		}
		
		// fetch horizontal spacing.
		NSObject* hSpacing = [sublayout objectForKey:@"horizontalSpacing"];
		if ([hSpacing isKindOfClass:[NSArray class]]) {
			NSUInteger i = 0;
			for (NSNumber* n in (NSArray*)hSpacing) {
				horizontalSpacings[i] = [n floatValue];
				++ i;
				if (i >= rows)
					break;
			}
		} else {
			CGFloat hsp = [(NSNumber*)hSpacing floatValue];
			for (NSUInteger i = 0; i < rows; ++ i)
				horizontalSpacings[i] = hsp;
		}
		
		// compute width and perform portrait-to-landscape transformation if needed.
		minWidthForPopupChar = UIKBKeyPopupSize.width;
		for (NSUInteger i = 0; i < rows; ++ i) {
			if (landscape) {
				if (i == rows-1) {
					lefts[i] = LandscapeMargin+LastRowHorizontalOffset+(lefts[i]-LastRowHorizontalOffset)*Ratio;
					rights[i] = LandscapeMargin+LastRowHorizontalOffset+(rights[i]-LastRowHorizontalOffset)*Ratio;
				} else {
					lefts[i] = LandscapeMargin + lefts[i]*Ratio;
					rights[i] = LandscapeMargin + rights[i]*Ratio;
				}
				horizontalSpacings[i] *= Ratio;
			}
			if (counts[i] != 0) {
				widths[i] = (keyboardWidth - lefts[i] - rights[i] - horizontalSpacings[i]*(counts[i]-1))/counts[i];
				if (widths[i] > minWidthForPopupChar)
					minWidthForPopupChar = widths[i];
			}
		}
		minWidthForPopupChar -= 2*Padding;
		
		// fetch texts & stuff.
#define DoFetch(var) for(NSUInteger i = 0; i < rows; ++ i) \
	var[i] = fetchTextRow(@#var, i, sublayout, layoutDict, 0)
		DoFetch(texts);
		DoFetch(shiftedTexts);
		DoFetch(labels);
		DoFetch(shiftedLabels);
		DoFetch(popups);
		DoFetch(shiftedPopups);
#undef DoFetch
		
		// set existence of special keys
		NSNumber* tmp;
#define SetObj(var, type, def) tmp = [sublayout objectForKey:@#var]; \
if (tmp != nil) \
	var = [tmp type##Value]; \
else \
	var = def
		
		SetObj(hasSpaceKey, bool, YES);
		SetObj(hasInternationalKey, bool, YES);
		SetObj(hasReturnKey, bool, YES);
		SetObj(hasShiftKey, bool, YES);
		SetObj(hasDeleteKey, bool, YES);
		if (hasShiftKey) {
			SetObj(shiftKeyLeft, float, 0);
			SetObj(shiftKeyWidth, float, ShiftDeleteDefaultWidth);
			if (landscape) {
				shiftKeyLeft = LandscapeMargin + shiftKeyLeft * Ratio;
				shiftKeyWidth *= Ratio;
			}
			SetObj(shiftKeyEnabled, bool, YES);
			NSString* stringifiedShiftStyle = [[sublayout objectForKey:@"shiftStyle"] lowercaseString];
			if (stringifiedShiftStyle != nil) {
				if ([@"123" isEqualToString:stringifiedShiftStyle])
					shiftStyle = UIKBShiftStyle123;
				else if ([@"symbol" isEqualToString:stringifiedShiftStyle])
					shiftStyle = UIKBShiftStyleSymbol;
				else
					shiftStyle = UIKBShiftStyleDefault;
			}
			SetObj(shiftKeyRow, integer, rows-2);
		}
		if (hasDeleteKey) {
			SetObj(deleteKeyRight, float, 0);
			SetObj(deleteKeyWidth, float, ShiftDeleteDefaultWidth);
			if (landscape) {
				deleteKeyRight = LandscapeMargin + deleteKeyRight * Ratio;
				deleteKeyWidth *= Ratio;
			}
			SetObj(deleteKeyRow, integer, rows-2);
		}
#undef SetObj
		
		// done :)
		
	}
	return self;
}

+(UIKBStandardKeyboard*)keyboardWithPlist:(NSDictionary*)layoutDict name:(NSString*)name landscape:(BOOL)landsc appearance:(UIKeyboardAppearance)appr {
	return [[[UIKBStandardKeyboard alloc] initWithPlist:layoutDict name:name landscape:landsc appearance:appr] autorelease];
}
+(UIKBStandardKeyboard*)keyboardWithBundle:(KeyboardBundle*)bdl name:(NSString*)name landscape:(BOOL)landsc appearance:(UIKeyboardAppearance)appr {
	NSString* layoutName = [bdl objectForInfoDictionaryKey:@"UIKeyboardLayoutClass"];
	if (![layoutName isKindOfClass:[NSString class]])
		return nil;
	
	NSString* layoutPath = [bdl pathForResource:layoutName ofType:nil];
	if (layoutPath == nil)
		return nil;
	
	return [[[UIKBStandardKeyboard alloc] initWithPlist:[NSDictionary dictionaryWithContentsOfFile:layoutPath]
												   name:name landscape:landsc appearance:appr] autorelease];
}

@dynamic image, shiftImage;
-(UIImage*)image { return [self imageWithShift:NO]; }
-(UIImage*)shiftImage { return [self imageWithShift:YES]; }
@dynamic fgImage, fgShiftImage;
-(UIImage*)fgImage { return [self fgImageWithShift:NO]; }
-(UIImage*)fgShiftImage { return [self fgImageWithShift:YES]; }

@dynamic keyDefinitions;


#define PopupMaxFontSize 45
#define LocationABC(l)           (l?CGPointMake(  0,124):CGPointMake(  0,173))
#define LocationInternational(l) (l?CGPointMake( 52,124):CGPointMake( 43,173))
#define LocationSpace(l)         (l?CGPointMake( 99,125):CGPointMake( 80,172))
#define LocationReturn(l)        (l?CGPointMake(382,125):CGPointMake(240,172))
#define RowDivider(l) (l?40:54)
#define UIKBKeyMaxFontSize 22.5f

-(UIImage*)fgImageWithShift:(BOOL)shift {
	UIGraphicsBeginImageContext(CGSizeMake(minWidthForPopupChar, UIKBKeyPopupSize.height*totalCount));
	
	float keyB = GUAverageLuminance(UIKBGetImage(UIKBImagePopupCenter, keyboardAppearance, landscape).CGImage);
	if (keyB <= 0.5)
		[[UIColor whiteColor] setFill];
	else
		[[UIColor blackColor] setFill];
	
	NSArray** myTexts = shift ? shiftedPopups : popups;
	UIFont* defaultFont = [UIFont boldSystemFontOfSize:PopupMaxFontSize];
	CGFloat h = 0;
	for (NSUInteger i = 0; i < rows; ++ i) {
		NSUInteger j = 0;
		for (NSString* pop in myTexts[i]) {
			drawInCenter(pop, CGRectMake(0, h, widths[i], UIKBKeyPopupSize.height), defaultFont);
			++ j;
			h += UIKBKeyPopupSize.height;
			if (j >= counts[i])
				break;
		}
		h += UIKBKeyPopupSize.height * (counts[i] - j);
	}
	
	UIImage* retImg = UIGraphicsGetImageFromCurrentImageContext();
	UIGraphicsEndImageContext();
	return retImg;
}

-(UIImage*)imageWithShift:(BOOL)shift {	
	UIGraphicsBeginImageContext(keyboardSize);
	
	// draw the background image.
	UIImage* bgImage = UIKBGetImage(UIKBImageBackground, keyboardAppearance, landscape);
	CGRect bgImgRect = CGRectZero;
	bgImgRect.size = bgImage.size;
	// FIXME: can't tile it.
	[bgImage drawInRect:CGRectMake(0, 0, keyboardSize.width, keyboardSize.height)];
	
	// draw last row stuff.
	if (hasInternationalKey) {
		[UIKBGetImage(UIKBImageABC, keyboardAppearance, landscape) drawAtPoint:LocationABC(landscape)];
		[UIKBGetImage(UIKBImageInternational, keyboardAppearance, landscape) drawAtPoint:LocationInternational(landscape)];
	}
	if (hasSpaceKey)
		[UIKBGetImage(UIKBImageSpace, keyboardAppearance, landscape) drawAtPoint:LocationSpace(landscape)];
	if (hasReturnKey)
		[UIKBGetImage(UIKBImageReturn, keyboardAppearance, landscape) drawAtPoint:LocationReturn(landscape)];
	
	// draw rows of keys except last
	NSArray** myTexts = shift ? shiftedLabels : labels;
	CGFloat top = verticalOffset;
	UIFont* defaultFont = [UIFont boldSystemFontOfSize:UIKBKeyMaxFontSize*keyHeight/DefaultHeight(landscape)];
	NSUInteger xheight = keyHeight;
	for (NSUInteger i = 0; i < rows; ++ i, top += keyHeight+verticalSpacing) {
		if (counts[i] == 0)
			continue;
		
		if (i == rows-1) {
			xheight = DefaultHeight(landscape);
			top = keyboardSize.height - xheight;
		}
		
		CGImageRef keyImg = UIKBGetImage(UIKBImageKeyRow0+((NSUInteger)top/RowDivider(landscape)), keyboardAppearance, landscape).CGImage;
		CGRect imgRect = CGRectIntegral(CGRectMake(lefts[i], top, widths[i], xheight));
		float imgLuma = GUAverageLuminance(keyImg);
		UIImage* resizedImage = GUCreateUIImageAndRelease(GUImageCreateWithCaps(keyImg,
																				CGRectMake(0, 0, imgRect.size.width, imgRect.size.height),
																				GUCapsMake(Padding, Padding, Padding, Padding)));
		NSUInteger j = 0;
		
		if (imgLuma <= 0.5)
			[[UIColor whiteColor] setFill];
		else
			[[UIColor blackColor] setFill];
		
		CGFloat left = lefts[i];
		for (NSString* lbl in myTexts[i]) {
			if (keyboardAppearance == UIKeyboardAppearanceAlert)
				[resizedImage drawInRect:imgRect blendMode:kCGBlendModeDestinationOut alpha:1];
			[resizedImage drawInRect:imgRect];
			drawInCenter(lbl, imgRect, defaultFont);
			
			left += widths[i]+horizontalSpacings[i];
			imgRect.origin.x = roundf(left);
			++ j;
			if (j >= counts[i])
				break;
		}
	}
	
	// draw the shift key.
	if (hasShiftKey) {
		CGRect shiftRect = CGRectIntegral(CGRectMake(shiftKeyLeft, verticalOffset+shiftKeyRow*(keyHeight+verticalSpacing),
													 shiftKeyWidth, keyHeight));
		UIKBImageClassType img = UIKBImageShift;
		if (!shiftKeyEnabled)
			img = UIKBImageShiftDisabled;
		else {
			if (shiftStyle == UIKBShiftStyle123)
				img = shift ? UIKBImageShiftSymbol : UIKBImageShift123;
			else if (shiftStyle == UIKBShiftStyleSymbol)
				img = shift ? UIKBImageShift123 : UIKBImageShiftSymbol;
		}
		UIImage* theImg = GUCreateUIImageAndRelease(GUImageCreateWithCaps(UIKBGetImage(img, keyboardAppearance, landscape).CGImage,
																		  CGRectMake(0, 0, shiftRect.size.width, shiftRect.size.height),
																		  GUCapsMake(Padding, Padding, Padding, Padding)));
		if (shiftStyle != UIKBShiftStyleDefault && keyboardAppearance == UIKeyboardAppearanceAlert)
			[theImg drawInRect:shiftRect blendMode:kCGBlendModeDestinationOut alpha:1];
		[theImg drawInRect:shiftRect];
	}
	
	// draw the delete key
	if (hasDeleteKey) {
		CGRect deleteRect = CGRectIntegral(CGRectMake(keyboardSize.width-deleteKeyWidth-deleteKeyRight,
													  verticalOffset+deleteKeyRow*(keyHeight+verticalSpacing),
													  deleteKeyWidth, keyHeight));
		[GUCreateUIImageAndRelease(GUImageCreateWithCaps(UIKBGetImage(UIKBImageDelete, keyboardAppearance, landscape).CGImage,
														 CGRectMake(0, 0, deleteRect.size.width, deleteRect.size.height),
														 GUCapsMake(Padding, Padding, Padding, Padding))) drawInRect:deleteRect];
	}
	
	UIImage* retImg = UIGraphicsGetImageFromCurrentImageContext();
	UIGraphicsEndImageContext();
	return retImg;
	
}


@end

