// Copyright <--\, Inc. All Rights Reserved.

#include "Building/DeepLevelBuildingVolFinalSolver.h"

namespace DeepLevelBuildingVolFinal
{
	namespace
	{
		constexpr double Small = 1.0e-8;

		double Cross(const FVector2D& A, const FVector2D& B)
		{
			return A.X * B.Y - A.Y * B.X;
		}

		bool PointOnSegment(const FVector2D& Point, const FVector2D& A, const FVector2D& B,
			const double Tolerance = 1.0e-7)
		{
			const FVector2D AB = B - A;
			const double Length = AB.Length();
			if (Length <= Small) { return FVector2D::Distance(Point, A) <= Tolerance; }
			return FMath::Abs(Cross(AB, Point - A)) <= Tolerance * Length
				&& FVector2D::DotProduct(Point - A, AB) >= -Tolerance * Length
				&& FVector2D::DotProduct(Point - B, AB) <= Tolerance * Length;
		}

		bool PointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
		{
			bool bInside = false;
			for (int32 I = 0, J = Polygon.Num() - 1; I < Polygon.Num(); J = I++)
			{
				const FVector2D& A = Polygon[J];
				const FVector2D& B = Polygon[I];
				if (PointOnSegment(Point, A, B)) { return true; }
				if ((A.Y > Point.Y) != (B.Y > Point.Y)
					&& Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X)
				{
					bInside = !bInside;
				}
			}
			return bInside;
		}

		bool SegmentContained(const FVector2D& A, const FVector2D& B, const TArray<FVector2D>& Polygon)
		{
			if (!PointInPolygon(A, Polygon) || !PointInPolygon(B, Polygon)) { return false; }
			const FVector2D R = B - A;
			const double RR = R.SquaredLength();
			if (RR <= Small * Small) { return true; }
			TArray<double> Cuts{0.0, 1.0};
			for (int32 Index = 0; Index < Polygon.Num(); ++Index)
			{
				const FVector2D C = Polygon[Index];
				const FVector2D D = Polygon[(Index + 1) % Polygon.Num()];
				const FVector2D S = D - C;
				const FVector2D Offset = C - A;
				const double Denominator = Cross(R, S);
				if (FMath::Abs(Denominator) > Small)
				{
					const double T = Cross(Offset, S) / Denominator;
					const double U = Cross(Offset, R) / Denominator;
					if (T >= 0.0 && T <= 1.0 && U >= -Small && U <= 1.0 + Small) { Cuts.Add(T); }
				}
				else if (PointOnSegment(C, A, B) || PointOnSegment(D, A, B))
				{
					Cuts.Add(FMath::Clamp(FVector2D::DotProduct(C - A, R) / RR, 0.0, 1.0));
					Cuts.Add(FMath::Clamp(FVector2D::DotProduct(D - A, R) / RR, 0.0, 1.0));
				}
			}
			Cuts.StableSort();
			for (int32 Index = 1; Index < Cuts.Num(); ++Index)
			{
				if (Cuts[Index] - Cuts[Index - 1] <= UE_DOUBLE_SMALL_NUMBER) { continue; }
				if (!PointInPolygon(A + R * ((Cuts[Index] + Cuts[Index - 1]) * 0.5), Polygon)) { return false; }
			}
			return true;
		}
	}

	void GetShapeCorners(const FFootprint& Footprint, TStaticArray<FVector2D, 4>& OutCorners)
	{
		const FVector2D Center(Footprint.Center);
		const FVector2D Forward(Footprint.Forward);
		const FVector2D Right(Footprint.Right);
		OutCorners[0] = Center - Forward * Footprint.HalfWidth - Right * Footprint.HalfDepth;
		OutCorners[1] = Center + Forward * Footprint.HalfWidth - Right * Footprint.HalfDepth;
		OutCorners[2] = Center + Forward * Footprint.HalfWidth + Right * Footprint.HalfDepth;
		OutCorners[3] = Center - Forward * Footprint.HalfWidth + Right * Footprint.HalfDepth;
	}

	double ProjectRadius(const FFootprint& Footprint, const FVector2D& Axis)
	{
		return FMath::Abs(FVector2D::DotProduct(FVector2D(Footprint.Forward), Axis)) * Footprint.HalfWidth
			+ FMath::Abs(FVector2D::DotProduct(FVector2D(Footprint.Right), Axis)) * Footprint.HalfDepth;
	}

	bool FootprintsOverlap(const FFootprint& A, const FFootprint& B)
	{
		if (A.MaxZ <= B.MinZ + 1.0 || B.MaxZ <= A.MinZ + 1.0) { return false; }
		const FVector2D Delta = FVector2D(B.Center - A.Center);
		const FVector2D Axes[] = {
			FVector2D(A.Forward), FVector2D(A.Right), FVector2D(B.Forward), FVector2D(B.Right)};
		for (const FVector2D& Axis : Axes)
		{
			if (FMath::Abs(FVector2D::DotProduct(Delta, Axis))
				>= ProjectRadius(A, Axis) + ProjectRadius(B, Axis) - 1.0)
			{
				return false;
			}
		}
		return true;
	}

	bool FootprintContained(const FFootprint& Footprint, const TArray<FVector2D>& Polygon)
	{
		TStaticArray<FVector2D, 4> Corners;
		GetShapeCorners(Footprint, Corners);
		for (int32 Index = 0; Index < Corners.Num(); ++Index)
		{
			if (!SegmentContained(Corners[Index], Corners[(Index + 1) % Corners.Num()], Polygon)) { return false; }
		}
		return true;
	}

	FPlacement Translate(const FPlacement& Placement, const double DeltaX, const double DeltaY)
	{
		FPlacement Result = Placement;
		const FVector Delta(DeltaX, DeltaY, 0.0);
		const double PathDelta = DeltaX * Placement.PathSample.Forward.X + DeltaY * Placement.PathSample.Forward.Y;
		Result.Distance += PathDelta;
		Result.CoverageStart += PathDelta;
		Result.CoverageEnd += PathDelta;
		Result.Center += Delta;
		Result.Facade.Start += Delta;
		Result.Facade.End += Delta;
		Result.Footprint.Center += Delta;
		Result.PathSample.Location += Delta;
		return Result;
	}

	TArray<FPlacement> AlignQuadGreedy(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		const double MaximumShift,
		const int32 MaximumPasses)
	{
		if (Polygon.Num() != 4 || Source.IsEmpty()) { return Source; }
		struct FSegment { FVector2D Forward; };
		TArray<FSegment> Segments;
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			Segments.Add({(Polygon[(Index + 1) % Polygon.Num()] - Polygon[Index]).GetSafeNormal()});
		}
		auto TangentFor = [&Segments](const FPlacement& Placement)
		{
			const FVector2D Facade = FVector2D(Placement.Facade.End - Placement.Facade.Start);
			if (Facade.Length() < 1.0e-4) { return Segments[0].Forward; }
			const FVector2D Direction = Facade.GetSafeNormal();
			int32 Best = 0;
			double BestDot = TNumericLimits<double>::Lowest();
			for (int32 Index = 0; Index < Segments.Num(); ++Index)
			{
				const double Value = FVector2D::DotProduct(Segments[Index].Forward, Direction);
				if (Value > BestDot) { BestDot = Value; Best = Index; }
			}
			return Segments[Best].Forward;
		};
		auto IsPlacementValid = [&Polygon](const FPlacement& Candidate, const int32 Moving,
			const TArray<FPlacement>& Placements)
		{
			if (!FootprintContained(Candidate.Footprint, Polygon)) { return false; }
			for (int32 Index = 0; Index < Placements.Num(); ++Index)
			{
				if (Index != Moving && FootprintsOverlap(Candidate.Footprint, Placements[Index].Footprint)) { return false; }
			}
			return true;
		};
		auto MaximumSlide = [&](const int32 Moving, const TArray<FPlacement>& Placements,
			const FVector2D& Direction)
		{
			if (!IsPlacementValid(Translate(Placements[Moving], Direction.X, Direction.Y), Moving, Placements)) { return 0.0; }
			double Low = 0.0;
			double Step = 10.0;
			while (Step <= MaximumShift
				&& IsPlacementValid(Translate(Placements[Moving], Direction.X * Step, Direction.Y * Step), Moving, Placements))
			{
				Low = Step;
				Step *= 2.0;
			}
			double High = FMath::Min(Step, MaximumShift);
			for (int32 Iteration = 0; Iteration < 16; ++Iteration)
			{
				const double Mid = (Low + High) * 0.5;
				if (IsPlacementValid(Translate(Placements[Moving], Direction.X * Mid, Direction.Y * Mid), Moving, Placements))
				{
					Low = Mid;
				}
				else { High = Mid; }
			}
			return FMath::FloorToDouble(Low);
		};

		TArray<FPlacement> Placements = Source;
		for (int32 Pass = 0; Pass < MaximumPasses; ++Pass)
		{
			double Moved = 0.0;
			for (int32 Index = 0; Index < Placements.Num(); ++Index)
			{
				const FVector2D Direction = -TangentFor(Placements[Index]);
				const double Slide = MaximumSlide(Index, Placements, Direction);
				if (Slide >= 1.0)
				{
					Placements[Index] = Translate(Placements[Index], Direction.X * Slide, Direction.Y * Slide);
					Moved += Slide;
				}
			}
			if (Moved < 1.0) { break; }
		}
		Placements.StableSort([](const FPlacement& A, const FPlacement& B) { return A.Distance < B.Distance; });
		return Placements;
	}

	namespace
	{
		struct FQuadFrame
		{
			FVector2D Origin;
			FVector2D U;
			FVector2D V;
			double ULength = 0.0;
			double VLength = 0.0;
		};

		struct FContact
		{
			int32 First = INDEX_NONE;
			int32 Second = INDEX_NONE;
			FVector2D Axis;
			double Sign = 1.0;
			double Target = 0.0;
			double Separation = 0.0;
		};

		bool MakeFrame(const TArray<FVector2D>& Points, FQuadFrame& Out)
		{
			if (Points.Num() != 4) { return false; }
			const FVector2D UDelta = Points[1] - Points[0];
			const FVector2D VDelta = Points[3] - Points[0];
			Out.Origin = Points[0];
			Out.ULength = UDelta.Length();
			Out.VLength = VDelta.Length();
			Out.U = UDelta / Out.ULength;
			Out.V = VDelta / Out.VLength;
			return FMath::IsFinite(Out.ULength) && FMath::IsFinite(Out.VLength)
				&& FMath::Abs(FVector2D::DotProduct(Out.U, Out.V)) <= 0.02;
		}

		FVector2D ToLocal(const FVector2D& Point, const FQuadFrame& Frame)
		{
			const FVector2D Offset = Point - Frame.Origin;
			return {FVector2D::DotProduct(Offset, Frame.U), FVector2D::DotProduct(Offset, Frame.V)};
		}

		FPlacement ClampToBlock(const FPlacement& Placement, const FQuadFrame& Frame)
		{
			TStaticArray<FVector2D, 4> Corners;
			GetShapeCorners(Placement.Footprint, Corners);
			double MinX = TNumericLimits<double>::Max(), MinY = TNumericLimits<double>::Max();
			double MaxX = TNumericLimits<double>::Lowest(), MaxY = TNumericLimits<double>::Lowest();
			for (const FVector2D& Corner : Corners)
			{
				const FVector2D Local = ToLocal(Corner, Frame);
				MinX = FMath::Min(MinX, Local.X); MaxX = FMath::Max(MaxX, Local.X);
				MinY = FMath::Min(MinY, Local.Y); MaxY = FMath::Max(MaxY, Local.Y);
			}
			const double X = MinX < 0.0 ? -MinX : MaxX > Frame.ULength ? Frame.ULength - MaxX : 0.0;
			const double Y = MinY < 0.0 ? -MinY : MaxY > Frame.VLength ? Frame.VLength - MaxY : 0.0;
			return FMath::Abs(X) + FMath::Abs(Y) > 1.0e-6
				? Translate(Placement, Frame.U.X * X + Frame.V.X * Y, Frame.U.Y * X + Frame.V.Y * Y)
				: Placement;
		}

		FContact PairConstraint(const FPlacement& A, const FPlacement& B, const int32 First, const int32 Second)
		{
			const FVector2D Delta = FVector2D(B.Center - A.Center);
			const FVector2D Axes[] = {FVector2D(A.Footprint.Forward), FVector2D(A.Footprint.Right),
				FVector2D(B.Footprint.Forward), FVector2D(B.Footprint.Right)};
			FContact Best;
			Best.First = First; Best.Second = Second;
			Best.Separation = TNumericLimits<double>::Lowest();
			for (const FVector2D& Axis : Axes)
			{
				const double Signed = FVector2D::DotProduct(Delta, Axis);
				const double Target = ProjectRadius(A.Footprint, Axis) + ProjectRadius(B.Footprint, Axis);
				const double Separation = FMath::Abs(Signed) - Target;
				if (Separation > Best.Separation)
				{
					Best.Axis = Axis; Best.Sign = Signed >= 0.0 ? 1.0 : -1.0;
					Best.Target = Target; Best.Separation = Separation;
				}
			}
			return Best;
		}

		double ContactError(const TArray<FPlacement>& Placements, const FContact& Contact)
		{
			return FVector2D::DotProduct(FVector2D(Placements[Contact.Second].Center - Placements[Contact.First].Center), Contact.Axis)
				* Contact.Sign - Contact.Target;
		}

		bool LayoutValid(const TArray<FPlacement>& Placements, const TArray<FVector2D>& Polygon)
		{
			for (int32 I = 0; I < Placements.Num(); ++I)
			{
				if (!FootprintContained(Placements[I].Footprint, Polygon)) { return false; }
				for (int32 J = I + 1; J < Placements.Num(); ++J)
				{
					if (FootprintsOverlap(Placements[I].Footprint, Placements[J].Footprint)) { return false; }
				}
			}
			return true;
		}
	}

	TArray<FPlacement> SolveV1(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		const int32 Iterations)
	{
		FQuadFrame Frame;
		if (!MakeFrame(Polygon, Frame) || Source.IsEmpty()) { return Source; }
		const TArray<FPlacement> GreedyWitness = AlignQuadGreedy(Polygon, Source);
		TArray<FPlacement> Witness;
		Witness.SetNum(Source.Num());
		for (const FPlacement& Placement : GreedyWitness)
		{
			if (Witness.IsValidIndex(Placement.SourceIndex)) { Witness[Placement.SourceIndex] = Placement; }
		}
		TArray<FContact> Contacts;
		for (int32 I = 0; I < Witness.Num(); ++I)
		{
			for (int32 J = I + 1; J < Witness.Num(); ++J)
			{
				const FContact Contact = PairConstraint(Witness[I], Witness[J], I, J);
				if (Contact.Separation <= 1.0) { Contacts.Add(Contact); }
			}
		}
		TArray<FPlacement> Placements = Source;
		auto ProjectContacts = [](TArray<FPlacement>& Current, const TArray<FContact>& CurrentContacts, const double Strength)
		{
			for (const FContact& Contact : CurrentContacts)
			{
				const double Gap = ContactError(Current, Contact);
				if (FMath::Abs(Gap) <= 1.0) { continue; }
				const double Amount = Gap * 0.5 * Strength;
				const FVector2D Delta = Contact.Axis * Contact.Sign * Amount;
				Current[Contact.First] = Translate(Current[Contact.First], Delta.X, Delta.Y);
				Current[Contact.Second] = Translate(Current[Contact.Second], -Delta.X, -Delta.Y);
			}
		};
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			ProjectContacts(Placements, Contacts, 0.9);
			for (int32 I = 0; I < Placements.Num(); ++I)
			{
				for (int32 J = I + 1; J < Placements.Num(); ++J)
				{
					const FContact Contact = PairConstraint(Placements[I], Placements[J], I, J);
					if (Contact.Separation >= 0.0) { continue; }
					const double Amount = (-Contact.Separation + 0.01) * 0.5;
					const FVector2D Delta = Contact.Axis * Contact.Sign * Amount;
					Placements[I] = Translate(Placements[I], -Delta.X, -Delta.Y);
					Placements[J] = Translate(Placements[J], Delta.X, Delta.Y);
				}
			}
			for (FPlacement& Placement : Placements) { Placement = ClampToBlock(Placement, Frame); }
			ProjectContacts(Placements, Contacts, 0.65);
		}
		double Remaining = 0.0;
		for (const FContact& Contact : Contacts) { Remaining += FMath::Max(0.0, ContactError(Placements, Contact)); }
		if (!LayoutValid(Placements, Polygon) || Remaining > FMath::Max(1, Contacts.Num())) { return Witness; }
		return Placements;
	}

	namespace
	{
		struct FBounds
		{
			double MinX = 0.0;
			double MaxX = 0.0;
			double MinY = 0.0;
			double MaxY = 0.0;
		};

		FBounds ProjectedBounds(const FPlacement& Placement, const FQuadFrame& Frame)
		{
			TStaticArray<FVector2D, 4> Corners;
			GetShapeCorners(Placement.Footprint, Corners);
			FBounds Result{TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest(),
				TNumericLimits<double>::Max(), TNumericLimits<double>::Lowest()};
			for (const FVector2D& Corner : Corners)
			{
				const FVector2D Local = ToLocal(Corner, Frame);
				Result.MinX = FMath::Min(Result.MinX, Local.X); Result.MaxX = FMath::Max(Result.MaxX, Local.X);
				Result.MinY = FMath::Min(Result.MinY, Local.Y); Result.MaxY = FMath::Max(Result.MaxY, Local.Y);
			}
			return Result;
		}

		void VisibleAdjacency(const TArray<FPlacement>& Placements, const FQuadFrame& Frame, TArray<FContact>& Out)
		{
			const int32 Count = Placements.Num();
			TArray<FBounds> Bounds;
			for (const FPlacement& Placement : Placements) { Bounds.Add(ProjectedBounds(Placement, Frame)); }
			struct FCandidate { FContact Contact; FName Axis; };
			TArray<FCandidate> Candidates;
			for (int32 I = 0; I < Count; ++I)
		{
				for (int32 J = I + 1; J < Count; ++J)
				{
					const FBounds& A = Bounds[I]; const FBounds& B = Bounds[J];
					const double OverlapY = FMath::Min(A.MaxY, B.MaxY) - FMath::Max(A.MinY, B.MinY);
					const double OverlapX = FMath::Min(A.MaxX, B.MaxX) - FMath::Max(A.MinX, B.MinX);
					if (OverlapY > 1.0 && (A.MaxX < B.MinX || B.MaxX < A.MinX))
					{
						const int32 Left = A.MaxX < B.MinX ? I : J; const int32 Right = Left == I ? J : I;
						const double Start = Bounds[Left].MaxX, End = Bounds[Right].MinX;
						const double BandMin = FMath::Max(Bounds[Left].MinY, Bounds[Right].MinY);
						const double BandMax = FMath::Min(Bounds[Left].MaxY, Bounds[Right].MaxY);
						bool bBlocked = false;
						for (int32 K = 0; K < Count && !bBlocked; ++K)
						{
							if (K != I && K != J && Bounds[K].MaxX > Start + 1.0 && Bounds[K].MinX < End - 1.0
								&& Bounds[K].MaxY > BandMin + 1.0 && Bounds[K].MinY < BandMax - 1.0) { bBlocked = true; }
						}
						if (!bBlocked)
						{
							FContact C{Left, Right, Frame.U, 1.0,
								ProjectRadius(Placements[Left].Footprint, Frame.U) + ProjectRadius(Placements[Right].Footprint, Frame.U), End - Start};
							Candidates.Add({C, TEXT("x")});
						}
					}
					if (OverlapX > 1.0 && (A.MaxY < B.MinY || B.MaxY < A.MinY))
					{
						const int32 Top = A.MaxY < B.MinY ? I : J; const int32 Bottom = Top == I ? J : I;
						const double Start = Bounds[Top].MaxY, End = Bounds[Bottom].MinY;
						const double BandMin = FMath::Max(Bounds[Top].MinX, Bounds[Bottom].MinX);
						const double BandMax = FMath::Min(Bounds[Top].MaxX, Bounds[Bottom].MaxX);
						bool bBlocked = false;
						for (int32 K = 0; K < Count && !bBlocked; ++K)
						{
							if (K != I && K != J && Bounds[K].MaxY > Start + 1.0 && Bounds[K].MinY < End - 1.0
								&& Bounds[K].MaxX > BandMin + 1.0 && Bounds[K].MinX < BandMax - 1.0) { bBlocked = true; }
						}
						if (!bBlocked)
						{
							FContact C{Top, Bottom, Frame.V, 1.0,
								ProjectRadius(Placements[Top].Footprint, Frame.V) + ProjectRadius(Placements[Bottom].Footprint, Frame.V), End - Start};
							Candidates.Add({C, TEXT("y")});
						}
					}
				}
			}
			for (int32 Index = 0; Index < Candidates.Num(); ++Index)
			{
				const FCandidate& Candidate = Candidates[Index];
				bool bNearestFirst = true, bNearestSecond = true;
				for (int32 Other = 0; Other < Candidates.Num(); ++Other)
				{
					if (Other == Index || Candidates[Other].Axis != Candidate.Axis) { continue; }
					if (Candidates[Other].Contact.First == Candidate.Contact.First
						&& Candidates[Other].Contact.Separation < Candidate.Contact.Separation) { bNearestFirst = false; }
					if (Candidates[Other].Contact.Second == Candidate.Contact.Second
						&& Candidates[Other].Contact.Separation < Candidate.Contact.Separation) { bNearestSecond = false; }
				}
				if (bNearestFirst && bNearestSecond) { Out.Add(Candidate.Contact); }
			}
		}

		struct FHullPoint { FVector2D Point; int32 Owner = INDEX_NONE; };
		void RubberContacts(const TArray<FPlacement>& Placements, TArray<FContact>& Out)
		{
			TArray<FHullPoint> Points;
			for (int32 Owner = 0; Owner < Placements.Num(); ++Owner)
			{
				TStaticArray<FVector2D, 4> Corners; GetShapeCorners(Placements[Owner].Footprint, Corners);
				for (const FVector2D& Point : Corners) { Points.Add({Point, Owner}); }
			}
			Points.StableSort([](const FHullPoint& A, const FHullPoint& B) { return A.Point.X == B.Point.X ? A.Point.Y < B.Point.Y : A.Point.X < B.Point.X; });
			TArray<FHullPoint> Hull;
			auto Turn = [](const FHullPoint& O, const FHullPoint& A, const FHullPoint& B) { return Cross(A.Point - O.Point, B.Point - O.Point); };
			for (const FHullPoint& Point : Points) { while (Hull.Num() >= 2 && Turn(Hull[Hull.Num()-2], Hull.Last(), Point) <= 0.0) Hull.Pop(); Hull.Add(Point); }
			const int32 Lower = Hull.Num();
			for (int32 I = Points.Num()-2; I >= 0; --I) { while (Hull.Num() > Lower && Turn(Hull[Hull.Num()-2], Hull.Last(), Points[I]) <= 0.0) Hull.Pop(); Hull.Add(Points[I]); }
			if (!Hull.IsEmpty()) Hull.Pop();
			TSet<uint64> Seen;
			for (int32 I = 0; I < Hull.Num(); ++I)
			{
				const int32 A = Hull[I].Owner, B = Hull[(I+1)%Hull.Num()].Owner;
				if (A == B) continue;
				const int32 Left = FMath::Min(A,B), Right = FMath::Max(A,B); const uint64 Key = (uint64(uint32(Left))<<32)|uint32(Right);
				if (Seen.Contains(Key)) continue; Seen.Add(Key);
				const FContact Contact = PairConstraint(Placements[Left], Placements[Right], Left, Right);
				if (Contact.Separation > 1.0) Out.Add(Contact);
			}
		}
	}

	TArray<FPlacement> SolveV2(const TArray<FVector2D>& Polygon, const TArray<FPlacement>& Source, const int32 Iterations)
	{
		FQuadFrame Frame; if (!MakeFrame(Polygon, Frame) || Source.IsEmpty()) return Source;
		const TArray<FPlacement> GreedyWitness = AlignQuadGreedy(Polygon, Source);
		TArray<FPlacement> Witness;
		Witness.SetNum(Source.Num());
		for (const FPlacement& Placement : GreedyWitness)
		{
			if (Witness.IsValidIndex(Placement.SourceIndex)) { Witness[Placement.SourceIndex] = Placement; }
		}
		TArray<FContact> WitnessContacts;
		for (int32 I=0;I<Witness.Num();++I) for(int32 J=I+1;J<Witness.Num();++J){const FContact C=PairConstraint(Witness[I],Witness[J],I,J);if(C.Separation<=1.0)WitnessContacts.Add(C);}
		TArray<FPlacement> Placements=Source;
		for(int32 Iteration=0;Iteration<Iterations;++Iteration)
		{
			TArray<FContact> Contacts=WitnessContacts, Added; RubberContacts(Placements,Added);
			for(const FContact& C:Added){bool bDuplicate=false;for(const FContact& E:Contacts)if(E.First==C.First&&E.Second==C.Second&&FMath::Abs(FVector2D::DotProduct(E.Axis,C.Axis))>.99){bDuplicate=true;break;}if(!bDuplicate)Contacts.Add(C);}
			TArray<int32> Degree;Degree.Init(0,Placements.Num());for(const FContact& C:Contacts){Degree[C.First]++;Degree[C.Second]++;}
			TArray<double> Mobility;for(int32 D:Degree)Mobility.Add(1.0/FMath::Pow(double(D+1),4.0));
			double StepScale=.32;bool bAccepted=false;
			for(int32 Attempt=0;Attempt<8&&!bAccepted;++Attempt)
			{
				TArray<FPlacement> Candidate=Placements;TArray<FVector2D> Delta;Delta.Init(FVector2D::ZeroVector,Candidate.Num());
				for(const FContact& C:Contacts){const double Gap=ContactError(Candidate,C);if(FMath::Abs(Gap)<=1.0)continue;const double Total=Mobility[C.First]+Mobility[C.Second];const FVector2D Axis=C.Axis*C.Sign;Delta[C.First]+=Axis*(Gap*StepScale*Mobility[C.First]/Total);Delta[C.Second]-=Axis*(Gap*StepScale*Mobility[C.Second]/Total);}
				for(int32 I=0;I<Candidate.Num();++I)Candidate[I]=Translate(Candidate[I],Delta[I].X,Delta[I].Y);
				for(int32 Pass=0;Pass<24;++Pass){TArray<FVector2D>OverlapDelta;OverlapDelta.Init(FVector2D::ZeroVector,Candidate.Num());for(int32 I=0;I<Candidate.Num();++I)for(int32 J=I+1;J<Candidate.Num();++J){const FContact C=PairConstraint(Candidate[I],Candidate[J],I,J);if(C.Separation>=0)continue;const double Total=Mobility[I]+Mobility[J],Depth=-C.Separation+.01;const FVector2D Axis=C.Axis*C.Sign;OverlapDelta[I]-=Axis*(Depth*Mobility[I]/Total);OverlapDelta[J]+=Axis*(Depth*Mobility[J]/Total);}for(int32 I=0;I<Candidate.Num();++I)Candidate[I]=Translate(Candidate[I],OverlapDelta[I].X,OverlapDelta[I].Y);for(FPlacement& P:Candidate)P=ClampToBlock(P,Frame);}
				if(LayoutValid(Candidate,Polygon)){Placements=MoveTemp(Candidate);bAccepted=true;}else StepScale*=.5;
			}
			if(!bAccepted)break;
		}
		return Placements;
	}

	TArray<FPlacement> SolveV3(
		const TArray<FVector2D>& Polygon,
		const TArray<FPlacement>& Source,
		const int32 Iterations,
		const double StepScale)
	{
		if (Polygon.Num() != 4 || Source.IsEmpty()) { return Source; }
		const TArray<FPlacement> Vol2 = SolveV2(Polygon, Source);
		const TArray<FPlacement> Witness = AlignQuadGreedy(Polygon, Source);
		TArray<FContact> WitnessContacts;
		for (int32 I = 0; I < Witness.Num(); ++I)
		{
			for (int32 J = I + 1; J < Witness.Num(); ++J)
			{
				const FContact Contact = PairConstraint(Witness[I], Witness[J], I, J);
				if (Contact.Separation <= 1.0) { WitnessContacts.Add(Contact); }
			}
		}
		bool bVol2Solved = LayoutValid(Vol2, Polygon);
		for (const FContact& Contact : WitnessContacts)
		{
			if (ContactError(Vol2, Contact) > 1.0) { bVol2Solved = false; break; }
		}
		if (bVol2Solved) { return Vol2; }

		const TArray<FPlacement> Target = SolveV1(Polygon, Source);
		TArray<FPlacement> Placements = Vol2;
		for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
		{
			TArray<FPlacement> Collective;
			Collective.Reserve(Placements.Num());
			for (int32 Index = 0; Index < Placements.Num(); ++Index)
			{
				const FVector Delta = (Target[Index].Center - Placements[Index].Center) * StepScale;
				Collective.Add(Translate(Placements[Index], Delta.X, Delta.Y));
			}
			if (LayoutValid(Collective, Polygon))
			{
				Placements = MoveTemp(Collective);
			}
			else
			{
				struct FOrder { int32 Index; double Distance; };
				TArray<FOrder> Order;
				for (int32 Index = 0; Index < Placements.Num(); ++Index)
				{
					Order.Add({Index, FVector::Distance(Target[Index].Center, Placements[Index].Center)});
				}
				Order.StableSort([](const FOrder& A, const FOrder& B) { return A.Distance > B.Distance; });
				bool bMoved = false;
				for (const FOrder& Entry : Order)
				{
					const int32 Index = Entry.Index;
					const FVector Delta = (Target[Index].Center - Placements[Index].Center) * StepScale;
					double Low = 0.0, High = 1.0;
					for (int32 Pass = 0; Pass < 14; ++Pass)
					{
						const double Mid = (Low + High) * 0.5;
						TArray<FPlacement> Trial = Placements;
						Trial[Index] = Translate(Placements[Index], Delta.X * Mid, Delta.Y * Mid);
						if (LayoutValid(Trial, Polygon)) { Low = Mid; } else { High = Mid; }
					}
					if (Low > 1.0e-4)
					{
						Placements[Index] = Translate(Placements[Index], Delta.X * Low, Delta.Y * Low);
						bMoved = true;
					}
				}
				if (!bMoved) { break; }
			}
			double Remaining = 0.0;
			for (int32 Index = 0; Index < Placements.Num(); ++Index)
			{
				Remaining += FVector::Distance(Target[Index].Center, Placements[Index].Center);
			}
			if (Remaining <= Placements.Num()) { break; }
		}
		return Placements;
	}

	namespace
	{
		struct FCell
		{
			double MinX=0,MaxX=0,MinY=0,MaxY=0;
			int32 Id=INDEX_NONE;
		};
		struct FWall { FVector2D Start,End; };
		struct FAssignment { int32 Index=INDEX_NONE,CellId=INDEX_NONE; bool bMovable=false; };
		struct FBody { FString Id; int32 Index=INDEX_NONE; bool bFixed=false; FFootprint Footprint; };
		struct FBodyContact { FBody A,B; FVector2D Axis; double Sign=1,Gap=0; };
		struct FTopology { TArray<TArray<int32>> Groups; TArray<FWall> ActiveWalls,Portals; };

		bool IsOrthogonal(const TArray<FVector2D>& Points)
		{
			for(int32 I=0;I<Points.Num();++I){const FVector2D D=Points[(I+1)%Points.Num()]-Points[I];if(FMath::Abs(D.X)>1&&FMath::Abs(D.Y)>1)return false;}return true;
		}
		TArray<double> UniqueSorted(TArray<double> Values)
		{
			Values.StableSort();TArray<double> Result;for(double V:Values)if(Result.IsEmpty()||Result.Last()!=V)Result.Add(V);return Result;
		}
		TArray<FCell> Decompose(const TArray<FVector2D>& Points)
		{
			TArray<double> Xs,Ys;for(const FVector2D& P:Points){Xs.Add(P.X);Ys.Add(P.Y);}Xs=UniqueSorted(MoveTemp(Xs));Ys=UniqueSorted(MoveTemp(Ys));
			TArray<FCell> Runs;
			for(int32 Y=0;Y+1<Ys.Num();++Y){TArray<TPair<double,double>> Active;for(int32 X=0;X+1<Xs.Num();++X)if(PointInPolygon({(Xs[X]+Xs[X+1])*.5,(Ys[Y]+Ys[Y+1])*.5},Points))Active.Add({Xs[X],Xs[X+1]});
				for(const auto& Cell:Active){if(!Runs.IsEmpty()&&Runs.Last().MinY==Ys[Y]&&FMath::Abs(Runs.Last().MaxX-Cell.Key)<=1)Runs.Last().MaxX=Cell.Value;else Runs.Add({Cell.Key,Cell.Value,Ys[Y],Ys[Y+1]});}}
			TArray<FCell> Rectangles;for(const FCell& Run:Runs){FCell* Previous=Rectangles.FindByPredicate([&](const FCell& R){return FMath::Abs(R.MinX-Run.MinX)<=1&&FMath::Abs(R.MaxX-Run.MaxX)<=1&&FMath::Abs(R.MaxY-Run.MinY)<=1;});if(Previous)Previous->MaxY=Run.MaxY;else Rectangles.Add(Run);}
			for(int32 I=0;I<Rectangles.Num();++I)Rectangles[I].Id=I;return Rectangles;
		}
		FBounds WorldBounds(const FFootprint& Footprint)
		{
			TStaticArray<FVector2D,4>C;GetShapeCorners(Footprint,C);FBounds B{TNumericLimits<double>::Max(),TNumericLimits<double>::Lowest(),TNumericLimits<double>::Max(),TNumericLimits<double>::Lowest()};for(const FVector2D& P:C){B.MinX=FMath::Min(B.MinX,P.X);B.MaxX=FMath::Max(B.MaxX,P.X);B.MinY=FMath::Min(B.MinY,P.Y);B.MaxY=FMath::Max(B.MaxY,P.Y);}return B;
		}
		bool InsideRect(const FBounds& B,const FCell& R){return B.MinX>=R.MinX-1&&B.MaxX<=R.MaxX+1&&B.MinY>=R.MinY-1&&B.MaxY<=R.MaxY+1;}
		bool RectContains(const FCell& R,const FVector& P){return P.X>=R.MinX-1&&P.X<=R.MaxX+1&&P.Y>=R.MinY-1&&P.Y<=R.MaxY+1;}
		TArray<FAssignment> AssignCells(const TArray<FPlacement>& Placements,const TArray<FCell>& Rectangles)
		{
			TArray<FAssignment> Result;for(int32 I=0;I<Placements.Num();++I){const FBounds B=WorldBounds(Placements[I].Footprint);const FCell* R=Rectangles.FindByPredicate([&](const FCell& C){return RectContains(C,Placements[I].Center);});Result.Add({I,R?R->Id:INDEX_NONE,R&&InsideRect(B,*R)});}return Result;
		}
		bool WallOnBoundary(const FWall& W,const TArray<FVector2D>& Points)
		{
			for(int32 I=0;I<Points.Num();++I){const FVector2D A=Points[I],B=Points[(I+1)%Points.Num()];if(FMath::Abs(A.X-W.Start.X)<=1&&FMath::Abs(B.X-W.End.X)<=1&&FMath::Min(A.Y,B.Y)<=FMath::Min(W.Start.Y,W.End.Y)+1&&FMath::Max(A.Y,B.Y)>=FMath::Max(W.Start.Y,W.End.Y)-1)return true;if(FMath::Abs(A.Y-W.Start.Y)<=1&&FMath::Abs(B.Y-W.End.Y)<=1&&FMath::Min(A.X,B.X)<=FMath::Min(W.Start.X,W.End.X)+1&&FMath::Max(A.X,B.X)>=FMath::Max(W.Start.X,W.End.X)-1)return true;}return false;
		}
		TArray<FWall> WallSegments(const TArray<FCell>& Rectangles,const TArray<FVector2D>& Points)
		{
			TArray<FWall> Result;auto Add=[&](FVector2D A,FVector2D B){for(const FWall& W:Result)if((W.Start.Equals(A)&&W.End.Equals(B))||(W.Start.Equals(B)&&W.End.Equals(A)))return;FWall W{A,B};if(!WallOnBoundary(W,Points))Result.Add(W);};for(const FCell&R:Rectangles){Add({R.MinX,R.MinY},{R.MaxX,R.MinY});Add({R.MaxX,R.MinY},{R.MaxX,R.MaxY});Add({R.MaxX,R.MaxY},{R.MinX,R.MaxY});Add({R.MinX,R.MaxY},{R.MinX,R.MinY});}return Result;
		}
		FBody BarBody(const FWall& Wall,const int32 Id)
		{
			const FVector2D Delta=Wall.End-Wall.Start;const double Length=Delta.Length();const FVector2D Forward=Delta/Length;FBody B;B.Id=FString::Printf(TEXT("bar:%d"),Id);B.bFixed=true;B.Footprint.Center=FVector((Wall.Start+Wall.End)*.5,0);B.Footprint.Forward=FVector(Forward,0);B.Footprint.Right=FVector(-Forward.Y,Forward.X,0);B.Footprint.HalfWidth=Length*.5;B.Footprint.HalfDepth=1;B.Footprint.MinZ=0;B.Footprint.MaxZ=200;return B;
		}
		bool BarTouchesRect(const FBody& Bar,const FCell& R){const FBounds B=WorldBounds(Bar.Footprint);return B.MaxX>=R.MinX-1&&B.MinX<=R.MaxX+1&&B.MaxY>=R.MinY-1&&B.MinY<=R.MaxY+1&&(FMath::Abs(B.MinX-R.MinX)<=2||FMath::Abs(B.MaxX-R.MaxX)<=2||FMath::Abs(B.MinY-R.MinY)<=2||FMath::Abs(B.MaxY-R.MaxY)<=2);}
		FTopology BuildTopology(const TArray<FCell>& Rectangles,const TArray<FWall>& Walls,const TArray<FPlacement>& Placements)
		{
			TArray<double> Spans;for(const FPlacement&P:Placements)Spans.Add(FMath::Min(P.Footprint.HalfWidth,P.Footprint.HalfDepth)*2);Spans.StableSort();const double Threshold=(Spans.IsEmpty()?0:Spans[FMath::FloorToInt(Spans.Num()*.5)])*.75;
			TArray<int32> Parent;for(int32 I=0;I<Rectangles.Num();++I)Parent.Add(I);auto Find=[&Parent](int32 I){while(Parent[I]!=I){Parent[I]=Parent[Parent[I]];I=Parent[I];}return I;};
			FTopology T;for(const FWall&W:Walls){const FVector2D M=(W.Start+W.End)*.5;TArray<int32> Cells;for(const FCell&R:Rectangles)if(M.X>=R.MinX-1&&M.X<=R.MaxX+1&&M.Y>=R.MinY-1&&M.Y<=R.MaxY+1&&(FMath::Abs(M.X-R.MinX)<=1||FMath::Abs(M.X-R.MaxX)<=1||FMath::Abs(M.Y-R.MinY)<=1||FMath::Abs(M.Y-R.MaxY)<=1))Cells.Add(R.Id);bool bTiny=false;for(int32 Id:Cells)if(FMath::Min(Rectangles[Id].MaxX-Rectangles[Id].MinX,Rectangles[Id].MaxY-Rectangles[Id].MinY)<Threshold)bTiny=true;if(bTiny&&Cells.Num()>1){for(int32 I=1;I<Cells.Num();++I){int32 A=Find(Cells[0]),B=Find(Cells[I]);if(A!=B)Parent[B]=A;}T.Portals.Add(W);}else T.ActiveWalls.Add(W);}
			for(const FCell&R:Rectangles){const int32 Root=Find(R.Id);TArray<int32>*G=T.Groups.FindByPredicate([&](const TArray<int32>& X){return !X.IsEmpty()&&Find(X[0])==Root;});if(G)G->Add(R.Id);else T.Groups.Add({R.Id});}return T;
		}
		FBodyContact BodyContact(const FBody& A,const FBody& B)
		{
			const FVector2D Delta=FVector2D(B.Footprint.Center-A.Footprint.Center);const FVector2D Axes[]={FVector2D(A.Footprint.Forward),FVector2D(A.Footprint.Right),FVector2D(B.Footprint.Forward),FVector2D(B.Footprint.Right)};FBodyContact Best;Best.A=A;Best.B=B;Best.Gap=TNumericLimits<double>::Lowest();for(const FVector2D& Axis:Axes){const double Signed=FVector2D::DotProduct(Delta,Axis),Target=ProjectRadius(A.Footprint,Axis)+ProjectRadius(B.Footprint,Axis),Gap=FMath::Abs(Signed)-Target;if(Gap>Best.Gap){Best.Axis=Axis;Best.Sign=Signed>=0?1:-1;Best.Gap=Gap;}}return Best;
		}
		void ConvexHullContacts(const TArray<FBody>& Bodies,TArray<FBodyContact>& Out)
		{
			struct FP{FVector2D Point;int32 Body;};TArray<FP>Points;for(int32 I=0;I<Bodies.Num();++I){TStaticArray<FVector2D,4>C;GetShapeCorners(Bodies[I].Footprint,C);for(const FVector2D&P:C)Points.Add({P,I});}if(Points.Num()<3)return;Points.StableSort([](const FP&A,const FP&B){return A.Point.X==B.Point.X?A.Point.Y<B.Point.Y:A.Point.X<B.Point.X;});auto Turn=[](const FP&O,const FP&A,const FP&B){return Cross(A.Point-O.Point,B.Point-O.Point);};TArray<FP>Hull;for(const FP&P:Points){while(Hull.Num()>=2&&Turn(Hull[Hull.Num()-2],Hull.Last(),P)<=0)Hull.Pop();Hull.Add(P);}const int32 Lower=Hull.Num();for(int32 I=Points.Num()-2;I>=0;--I){while(Hull.Num()>Lower&&Turn(Hull[Hull.Num()-2],Hull.Last(),Points[I])<=0)Hull.Pop();Hull.Add(Points[I]);}Hull.Pop();TSet<FString>Seen;for(int32 I=0;I<Hull.Num();++I){const FBody&A=Bodies[Hull[I].Body],&B=Bodies[Hull[(I+1)%Hull.Num()].Body];if(A.Id==B.Id||(A.bFixed&&B.bFixed))continue;const FString Key=A.Id<B.Id?A.Id+TEXT("|")+B.Id:B.Id+TEXT("|")+A.Id;if(Seen.Contains(Key))continue;Seen.Add(Key);FBodyContact C=BodyContact(A,B);if(C.Gap>1)Out.Add(C);}
		}
		TArray<FBodyContact> RubberContactsForGroups(const TArray<FPlacement>& Placements,const TArray<FAssignment>& Assignments,const TArray<FCell>& Rectangles,const TArray<FBody>& Bars,const TArray<TArray<int32>>& Groups)
		{
			TArray<FBodyContact>Result;TSet<FString>Seen;for(const TArray<int32>&Group:Groups){TArray<FBody>Bodies;for(const FAssignment&A:Assignments)if(Group.Contains(A.CellId)){FBody B;B.Id=FString::Printf(TEXT("building:%d"),A.Index);B.Index=A.Index;B.Footprint=Placements[A.Index].Footprint;Bodies.Add(B);}for(const FBody&Bar:Bars){bool bTouch=false;for(int32 Id:Group)if(BarTouchesRect(Bar,Rectangles[Id])){bTouch=true;break;}if(bTouch)Bodies.Add(Bar);}TArray<FBodyContact>Local;ConvexHullContacts(Bodies,Local);for(const FBodyContact&C:Local){const FString Key=C.A.Id<C.B.Id?C.A.Id+TEXT("|")+C.B.Id:C.B.Id+TEXT("|")+C.A.Id;if(!Seen.Contains(Key)){Seen.Add(Key);Result.Add(C);}}}return Result;
		}
		TOptional<TArray<FPlacement>> SafeRubberContact(const TArray<FPlacement>& Current,const FBodyContact& Contact,const TArray<FVector2D>& Points,const double Strength,const double MaxStep)
		{
			const double Amount=FMath::Min(MaxStep,Contact.Gap*Strength);TArray<FVector2D>Attempts;if(!Contact.A.bFixed&&!Contact.B.bFixed){Attempts.Add({Amount*.5,-Amount*.5});Attempts.Add({Amount,0});Attempts.Add({0,-Amount});}else if(!Contact.A.bFixed)Attempts.Add({Amount,0});else if(!Contact.B.bFixed)Attempts.Add({0,-Amount});const FVector2D Axis=Contact.Axis*Contact.Sign;
			for(const FVector2D&Moves:Attempts){auto Build=[&](double Alpha){TArray<FPlacement>Trial=Current;if(!Contact.A.bFixed)Trial[Contact.A.Index]=Translate(Current[Contact.A.Index],Axis.X*Moves.X*Alpha,Axis.Y*Moves.X*Alpha);if(!Contact.B.bFixed)Trial[Contact.B.Index]=Translate(Current[Contact.B.Index],Axis.X*Moves.Y*Alpha,Axis.Y*Moves.Y*Alpha);return Trial;};TArray<FPlacement>Full=Build(1);if(LayoutValid(Full,Points))return Full;double Low=0,High=1;for(int32 Pass=0;Pass<14;++Pass){const double Mid=(Low+High)*.5;if(LayoutValid(Build(Mid),Points))Low=Mid;else High=Mid;}if(Low>1e-3)return Build(Low);}return {};
		}
		TArray<FBodyContact> SeamContacts(const TArray<FPlacement>& Placements,const TArray<FAssignment>& Assignments,const TArray<TArray<int32>>& Groups,const double MaxGap)
		{
			TArray<FBounds>Bounds;for(const FPlacement&P:Placements)Bounds.Add(WorldBounds(P.Footprint));TArray<FBodyContact>Result;for(const TArray<int32>&Group:Groups){TArray<int32>Ids;for(const FAssignment&A:Assignments)if(Group.Contains(A.CellId))Ids.Add(A.Index);for(int32 X=0;X<Ids.Num();++X)for(int32 Y=X+1;Y<Ids.Num();++Y){const int32 I=Ids[X],J=Ids[Y];const FBounds&A=Bounds[I],&B=Bounds[J];const double OverlapY=FMath::Min(A.MaxY,B.MaxY)-FMath::Max(A.MinY,B.MinY),OverlapX=FMath::Min(A.MaxX,B.MaxX)-FMath::Max(A.MinX,B.MinX);double Gap=TNumericLimits<double>::Max();if(OverlapY>1)Gap=FMath::Max(0.0,FMath::Max(A.MinX,B.MinX)-FMath::Min(A.MaxX,B.MaxX));if(OverlapX>1)Gap=FMath::Min(Gap,FMath::Max(0.0,FMath::Max(A.MinY,B.MinY)-FMath::Min(A.MaxY,B.MaxY)));if(Gap<=1||Gap>MaxGap)continue;const double MinX=FMath::Min(A.MaxX,B.MaxX),MaxX=FMath::Max(A.MinX,B.MinX),MinY=FMath::Min(A.MaxY,B.MaxY),MaxY=FMath::Max(A.MinY,B.MinY);bool bBlocked=false;for(int32 K:Ids)if(K!=I&&K!=J&&Bounds[K].MaxX>MinX+1&&Bounds[K].MinX<MaxX-1&&Bounds[K].MaxY>MinY+1&&Bounds[K].MinY<MaxY-1){bBlocked=true;break;}if(!bBlocked){FBody BA;BA.Id=FString::Printf(TEXT("building:%d"),I);BA.Index=I;BA.Footprint=Placements[I].Footprint;FBody BB;BB.Id=FString::Printf(TEXT("building:%d"),J);BB.Index=J;BB.Footprint=Placements[J].Footprint;FBodyContact C=BodyContact(BA,BB);if(C.Gap<=MaxGap)Result.Add(C);}}}Result.StableSort([](const FBodyContact&A,const FBodyContact&B){return A.Gap<B.Gap;});return Result;
		}
	}

	TArray<FPlacement> SolveV7(const TArray<FVector2D>& Polygon,const TArray<FPlacement>& Source,const bool bFine)
	{
		if(Polygon.Num()<=4||Source.IsEmpty()||!IsOrthogonal(Polygon))return Source;const TArray<FCell>Rectangles=Decompose(Polygon);const TArray<FAssignment>Assignments=AssignCells(Source,Rectangles);const TArray<FWall>Walls=WallSegments(Rectangles,Polygon);const FTopology Topology=BuildTopology(Rectangles,Walls,Source);TArray<FBody>Bars;for(int32 I=0;I<Topology.ActiveWalls.Num();++I)Bars.Add(BarBody(Topology.ActiveWalls[I],I));TArray<FPlacement>Placements=Source;
		if(bFine){constexpr double MaxGap=300;for(int32 Iteration=0;Iteration<80;++Iteration){const TArray<FBodyContact>Contacts=SeamContacts(Placements,Assignments,Topology.Groups,MaxGap);if(Contacts.IsEmpty())break;bool bMoved=false;for(const FBodyContact&C:Contacts)if(TOptional<TArray<FPlacement>>Next=SafeRubberContact(Placements,C,Polygon,.5,FMath::Min(30.0,MaxGap))){Placements=MoveTemp(Next.GetValue());bMoved=true;}if(!bMoved)break;}return Placements;}
		for(int32 Iteration=0;Iteration<70;++Iteration){TArray<FBodyContact>Contacts=RubberContactsForGroups(Placements,Assignments,Rectangles,Bars,Topology.Groups);Contacts.StableSort([](const FBodyContact&A,const FBodyContact&B){return A.Gap>B.Gap;});bool bMoved=false;for(const FBodyContact&C:Contacts)if(TOptional<TArray<FPlacement>>Next=SafeRubberContact(Placements,C,Polygon,.25,60)){Placements=MoveTemp(Next.GetValue());bMoved=true;}if(!bMoved)break;}return Placements;
	}

	namespace
	{
		struct FSplineEdge{int32 Index=INDEX_NONE;FVector2D Start,End,Tangent;double Length=0;};
		struct FConstraint{FBodyContact Contact;FName Kind;};
		TArray<FSplineEdge> SplineEdges(const TArray<FVector2D>& Points){TArray<FSplineEdge>R;for(int32 I=0;I<Points.Num();++I){const FVector2D D=Points[(I+1)%Points.Num()]-Points[I];const double L=D.Length();R.Add({I,Points[I],Points[(I+1)%Points.Num()],D/L,L});}return R;}
		int32 EdgeFor(const FPlacement&P,const TArray<FSplineEdge>&Edges){int32 Best=0;double Score=TNumericLimits<double>::Max();const FVector2D Forward(P.PathSample.Forward),Center(P.Center);for(const FSplineEdge&E:Edges){if(FVector2D::DotProduct(Forward,E.Tangent)<.99)continue;const FVector2D D=Center-E.Start;const double Scalar=FVector2D::DotProduct(D,E.Tangent),Normal=FMath::Abs(D.X*-E.Tangent.Y+D.Y*E.Tangent.X),Value=Normal+FMath::Max(0.0,FMath::Max(-Scalar,Scalar-E.Length))*4;if(Value<Score){Score=Value;Best=E.Index;}}return Best;}
		FBody PlacementBody(const FPlacement&P,const int32 I){FBody B;B.Id=FString::Printf(TEXT("building:%d"),I);B.Index=I;B.Footprint=P.Footprint;return B;}
		void BuildConstraints(const TArray<FPlacement>&Placements,const TArray<FSplineEdge>&Edges,const TArray<FWall>&Walls,TArray<FConstraint>&Out,TArray<int32>&Ownership)
		{
			for(const FPlacement&P:Placements)Ownership.Add(EdgeFor(P,Edges));TSet<uint64>Seen;auto Add=[&](int32 I,int32 J,FName Kind){FBodyContact C=BodyContact(PlacementBody(Placements[I],I),PlacementBody(Placements[J],J));if(C.Gap<=1)return;const uint32 A=FMath::Min(I,J),B=FMath::Max(I,J);const uint64 K=(uint64(A)<<32)|B;if(!Seen.Contains(K)){Seen.Add(K);Out.Add({C,Kind});}};
			for(const FSplineEdge&E:Edges){TArray<TPair<int32,double>>Ids;for(int32 I=0;I<Placements.Num();++I)if(Ownership[I]==E.Index)Ids.Add({I,FVector2D::DotProduct(FVector2D(Placements[I].Center)-E.Start,E.Tangent)});Ids.StableSort([](const auto&A,const auto&B){return A.Value<B.Value;});for(int32 K=0;K+1<Ids.Num();++K)Add(Ids[K].Key,Ids[K+1].Key,TEXT("edge"));}
			const TArray<FBounds>Bounds=[&](){TArray<FBounds>R;for(const FPlacement&P:Placements)R.Add(WorldBounds(P.Footprint));return R;}();
			for(const FWall&W:Walls){const bool V=FMath::Abs(W.Start.X-W.End.X)<=1;const double Coord=V?W.Start.X:W.Start.Y,Min=FMath::Min(V?W.Start.Y:W.Start.X,V?W.End.Y:W.End.X),Max=FMath::Max(V?W.Start.Y:W.Start.X,V?W.End.Y:W.End.X);struct FC{int32 I;int32 Side;double Distance;};TArray<FC>Candidates;for(int32 I=0;I<Placements.Num();++I){const FBounds&B=Bounds[I];const double SpanMin=V?B.MinY:B.MinX,SpanMax=V?B.MaxY:B.MaxX;if(SpanMax<Min+1||SpanMin>Max-1)continue;const int32 Side=(V?Placements[I].Center.X:Placements[I].Center.Y)<Coord?-1:1;const double Dist=V?FMath::Min(FMath::Abs(B.MaxX-Coord),FMath::Abs(B.MinX-Coord)):FMath::Min(FMath::Abs(B.MaxY-Coord),FMath::Abs(B.MinY-Coord));Candidates.Add({I,Side,Dist});}Candidates.StableSort([](const FC&A,const FC&B){return A.Distance<B.Distance;});const FC*First=Candidates.FindByPredicate([](const FC&C){return C.Side<0;}),*Second=Candidates.FindByPredicate([](const FC&C){return C.Side>0;});if(First&&Second)Add(First->I,Second->I,TEXT("portal"));}
		}
		TOptional<TArray<FPlacement>> SafeConstraintStep(const TArray<FPlacement>&Current,const FConstraint&Constraint,const TArray<int32>&Ownership,const TArray<FSplineEdge>&Edges,const TArray<FVector2D>&Points)
		{
			const FBodyContact&C=Constraint.Contact;const FVector2D N=C.Axis*C.Sign,TA=Edges[Ownership[C.A.Index]].Tangent,TB=Edges[Ownership[C.B.Index]].Tangent;const double CA=FVector2D::DotProduct(N,TA),CB=-FVector2D::DotProduct(N,TB),Den=CA*CA+CB*CB;if(Den<1e-6)return{};const double Amount=FMath::Max(0.0,C.Gap-1);TArray<FVector2D>Attempts{{Amount*CA/Den,Amount*CB/Den}};if(FMath::Abs(CA)>1e-6)Attempts.Add({Amount/CA,0});if(FMath::Abs(CB)>1e-6)Attempts.Add({0,Amount/CB});for(const FVector2D&D:Attempts){auto Build=[&](double Alpha){TArray<FPlacement>T=Current;T[C.A.Index]=Translate(Current[C.A.Index],TA.X*D.X*Alpha,TA.Y*D.X*Alpha);T[C.B.Index]=Translate(Current[C.B.Index],TB.X*D.Y*Alpha,TB.Y*D.Y*Alpha);return T;};TArray<FPlacement>Full=Build(1);if(LayoutValid(Full,Points))return Full;double Low=0,High=1;for(int32 Pass=0;Pass<18;++Pass){const double Mid=(Low+High)*.5;if(LayoutValid(Build(Mid),Points))Low=Mid;else High=Mid;}if(Low>1e-3)return Build(Low);}return{};
		}
		struct FRectLink{int32 To;FVector2D Normal;};
		TArray<TArray<FRectLink>> RectangleGraph(const TArray<FCell>&R){TArray<TArray<FRectLink>>Links;Links.SetNum(R.Num());for(int32 I=0;I<R.Num();++I)for(int32 J=I+1;J<R.Num();++J){const FCell&A=R[I],&B=R[J];const double OY=FMath::Min(A.MaxY,B.MaxY)-FMath::Max(A.MinY,B.MinY),OX=FMath::Min(A.MaxX,B.MaxX)-FMath::Max(A.MinX,B.MinX);FVector2D N=FVector2D::ZeroVector;if(OY>1&&(FMath::Abs(A.MaxX-B.MinX)<=1||FMath::Abs(B.MaxX-A.MinX)<=1))N={FMath::Sign((B.MinX+B.MaxX)-(A.MinX+A.MaxX)),0};if(OX>1&&(FMath::Abs(A.MaxY-B.MinY)<=1||FMath::Abs(B.MaxY-A.MinY)<=1))N={0,FMath::Sign((B.MinY+B.MaxY)-(A.MinY+A.MaxY))};if(!N.IsNearlyZero()){Links[I].Add({J,N});Links[J].Add({I,-N});}}return Links;}
		double RectOverlapRatio(const FPlacement&P,const FCell&R){const FBounds B=WorldBounds(P.Footprint);const double X=FMath::Max(0.0,FMath::Min(B.MaxX,R.MaxX)-FMath::Max(B.MinX,R.MinX)),Y=FMath::Max(0.0,FMath::Min(B.MaxY,R.MaxY)-FMath::Max(B.MinY,R.MinY));return X*Y/FMath::Max(1.0,(B.MaxX-B.MinX)*(B.MaxY-B.MinY));}
		TArray<FPlacement> DockBranches(const TArray<FPlacement>&Source,const TArray<FCell>&Rectangles,const TArray<FVector2D>&Points)
		{
			if(Rectangles.IsEmpty())return Source;const auto Graph=RectangleGraph(Rectangles);int32 Root=0;for(int32 I=1;I<Rectangles.Num();++I)if((Rectangles[I].MaxX-Rectangles[I].MinX)*(Rectangles[I].MaxY-Rectangles[I].MinY)>(Rectangles[Root].MaxX-Rectangles[Root].MinX)*(Rectangles[Root].MaxY-Rectangles[Root].MinY))Root=I;TArray<int32>Leaves;for(const FCell&R:Rectangles)if(R.Id!=Root&&Graph[R.Id].Num()==1)Leaves.Add(R.Id);Leaves.StableSort([&](int32 A,int32 B){return (Rectangles[A].MaxX-Rectangles[A].MinX)*(Rectangles[A].MaxY-Rectangles[A].MinY)<(Rectangles[B].MaxX-Rectangles[B].MinX)*(Rectangles[B].MaxY-Rectangles[B].MinY);});TArray<FPlacement>Current=Source;
			for(int32 LeafId:Leaves){const FCell&Leaf=Rectangles[LeafId];const FRectLink Link=Graph[LeafId][0];const FCell&Parent=Rectangles[Link.To];TArray<int32>Moving,Fixed;for(int32 I=0;I<Current.Num();++I){if(RectContains(Leaf,Current[I].Center))Moving.Add(I);else if(RectContains(Parent,Current[I].Center))Fixed.Add(I);}if(Moving.IsEmpty()){struct FTip{int32 I;double Ratio;};TArray<FTip>Tips;TSet<int32>TipIndices;for(int32 I=0;I<Current.Num();++I){const double R=RectOverlapRatio(Current[I],Leaf);if(R>=.1){Tips.Add({I,R});TipIndices.Add(I);}}Tips.StableSort([](const FTip&A,const FTip&B){return A.Ratio>B.Ratio;});Fixed.RemoveAll([&](const int32 Index){return TipIndices.Contains(Index);});FBodyContact Best;bool bFound=false;for(const FTip&T:Tips)for(int32 J:Fixed){FBodyContact C=BodyContact(PlacementBody(Current[T.I],T.I),PlacementBody(Current[J],J));if(C.Gap>1&&(!bFound||C.Gap<Best.Gap)){Best=C;bFound=true;}}if(!bFound)continue;const FVector2D N=Best.Axis*Best.Sign;const double Distance=FMath::Max(0.0,Best.Gap-1);auto Build=[&](double A){TArray<FPlacement>T=Current;T[Best.A.Index]=Translate(T[Best.A.Index],N.X*Distance*A,N.Y*Distance*A);return T;};double Alpha=1;if(!LayoutValid(Build(1),Points)){double Low=0,High=1;for(int32 P=0;P<18;++P){double Mid=(Low+High)*.5;if(LayoutValid(Build(Mid),Points))Low=Mid;else High=Mid;}Alpha=Low;}if(Alpha>1e-3)Current=Build(Alpha);continue;}if(Fixed.IsEmpty())continue;const TArray<FBounds>Bounds=[&](){TArray<FBounds>R;for(const FPlacement&P:Current)R.Add(WorldBounds(P.Footprint));return R;}();double Gap=TNumericLimits<double>::Max();for(int32 I:Moving)for(int32 J:Fixed){const FBounds&A=Bounds[I],&B=Bounds[J];const double Lateral=Link.Normal.X?FMath::Min(A.MaxY,B.MaxY)-FMath::Max(A.MinY,B.MinY):FMath::Min(A.MaxX,B.MaxX)-FMath::Max(A.MinX,B.MinX);if(Lateral<=1)continue;const double S=Link.Normal.X>0?B.MinX-A.MaxX:Link.Normal.X<0?A.MinX-B.MaxX:Link.Normal.Y>0?B.MinY-A.MaxY:A.MinY-B.MaxY;if(S>1)Gap=FMath::Min(Gap,S);}if(!FMath::IsFinite(Gap))continue;const double Distance=FMath::Max(0.0,Gap-1);if(Distance<=1)continue;auto Build=[&](double A){TArray<FPlacement>T=Current;for(int32 I:Moving)T[I]=Translate(T[I],Link.Normal.X*Distance*A,Link.Normal.Y*Distance*A);return T;};double Alpha=1;if(!LayoutValid(Build(1),Points)){double Low=0,High=1;for(int32 P=0;P<18;++P){double Mid=(Low+High)*.5;if(LayoutValid(Build(Mid),Points))Low=Mid;else High=Mid;}Alpha=Low;}if(Alpha>1e-3)Current=Build(Alpha);}return Current;
		}
	}

	TArray<FPlacement> SolveV9Stage(const TArray<FVector2D>&Polygon,const TArray<FPlacement>&Source,const int32 StageIndex)
	{
		if(StageIndex==2)return SolveV7(Polygon,Source,false);if(Polygon.Num()<=4||Source.IsEmpty()||!IsOrthogonal(Polygon))return Source;const TArray<FCell>Rects=Decompose(Polygon);const TArray<FWall>Walls=WallSegments(Rects,Polygon);const TArray<FSplineEdge>Edges=SplineEdges(Polygon);TArray<FPlacement>Placements=DockBranches(Source,Rects,Polygon);const bool bFine=StageIndex==1;const int32 Iterations=bFine?100:70;
		for(int32 Iteration=0;Iteration<Iterations;++Iteration){TArray<FConstraint>Constraints;TArray<int32>Ownership;BuildConstraints(Placements,Edges,Walls,Constraints,Ownership);Constraints.StableSort([](const FConstraint&A,const FConstraint&B){return A.Contact.Gap<B.Contact.Gap;});bool bMoved=false;for(const FConstraint&C:Constraints)if(TOptional<TArray<FPlacement>>Next=SafeConstraintStep(Placements,C,Ownership,Edges,Polygon)){Placements=MoveTemp(Next.GetValue());bMoved=true;}if(!bMoved)break;}return Placements;
	}

	namespace
	{
		double PairGap(const FPlacement&A,const FPlacement&B){return BodyContact(PlacementBody(A,0),PlacementBody(B,1)).Gap;}
		TSet<int32> ProtectedBuildings(const TArray<FVector2D>&Polygon,const TArray<FPlacement>&Placements)
		{
			const TArray<FSplineEdge>Edges=SplineEdges(Polygon);TArray<int32>Owners;for(const FPlacement&P:Placements)Owners.Add(EdgeFor(P,Edges));TSet<int32>Result;double Area=0;for(int32 I=0;I<Polygon.Num();++I){const FVector2D&A=Polygon[I],&B=Polygon[(I+1)%Polygon.Num()];Area+=A.X*B.Y-B.X*A.Y;}const double Orientation=FMath::Sign(Area)==0?1:FMath::Sign(Area);
			for(int32 I=0;I<Polygon.Num();++I){const FVector2D A=Polygon[(I-1+Polygon.Num())%Polygon.Num()],B=Polygon[I],C=Polygon[(I+1)%Polygon.Num()];const double Turn=Cross(B-A,C-B);if(FMath::Sign(Turn)!=Orientation)for(int32 P=0;P<Placements.Num();++P){const FFootprint&F=Placements[P].Footprint;const FVector2D D=B-FVector2D(F.Center);if(FMath::Abs(FVector2D::DotProduct(D,FVector2D(F.Forward)))<=F.HalfWidth+1&&FMath::Abs(FVector2D::DotProduct(D,FVector2D(F.Right)))<=F.HalfDepth+1)Result.Add(P);}}
			for(const FSplineEdge&E:Edges){TArray<TPair<int32,double>>Ids;for(int32 I=0;I<Placements.Num();++I)if(Owners[I]==E.Index)Ids.Add({I,FVector2D::DotProduct(FVector2D(Placements[I].Center)-E.Start,E.Tangent)});Ids.StableSort([](const auto&A,const auto&B){return A.Value<B.Value;});for(int32 I=1;I+1<Ids.Num();++I)if(PairGap(Placements[Ids[I-1].Key],Placements[Ids[I].Key])<=1&&PairGap(Placements[Ids[I].Key],Placements[Ids[I+1].Key])<=1)Result.Add(Ids[I].Key);}return Result;
		}
		void RecordPhase(FResult&Result,FName Id,const TArray<FPlacement>&Before,const TArray<FPlacement>&After,int32 Protected=0,int32 Accepted=0)
		{
			FPhase Phase;Phase.Id=Id;Phase.ProtectedBuildingCount=Protected;Phase.AcceptedMoveCount=Accepted;for(int32 I=0;I<Before.Num();++I){const double D=FVector::Distance(Before[I].Center,After[I].Center);if(D>1)Phase.BuildingsMovedCount++;Phase.TotalShiftCm+=D;}Result.Phases.Add(Phase);Result.Placements=After;Result.PhasePlacements.Add(After);
		}
	}

	FResult Solve(const TArray<FVector2D>&Polygon,const TArray<FPlacement>&Source)
	{
		FResult Result;Result.Placements=Source;if(Polygon.Num()<3||Source.IsEmpty())return Result;auto Apply=[&](FName Id,TFunctionRef<TArray<FPlacement>(const TArray<FPlacement>&)>Function){const TArray<FPlacement>Before=Result.Placements;RecordPhase(Result,Id,Before,Function(Before));};
		if(Polygon.Num()==4){Apply(TEXT("quad-rubber"),[&](const auto&P){return SolveV2(Polygon,P);});Apply(TEXT("quad-safe-step"),[&](const auto&P){return SolveV3(Polygon,P);});}
		for(int32 Stage=0;Stage<3;++Stage)Apply(*FString::Printf(TEXT("v9-%d-first"),Stage+1),[&](const auto&P){return SolveV9Stage(Polygon,P,Stage);});
		Apply(TEXT("v7-1"),[&](const auto&P){return SolveV7(Polygon,P,false);});
		{
			const TArray<FPlacement>Before=Result.Placements,Candidate=SolveV7(Polygon,Before,true);const TSet<int32>Protected=ProtectedBuildings(Polygon,Before);TArray<FPlacement>AcceptedPlacements=Before;int32 Accepted=0;for(int32 I=0;I<Before.Num();++I){if(Protected.Contains(I))continue;TArray<FPlacement>Trial=AcceptedPlacements;Trial[I]=Candidate[I];if(LayoutValid(Trial,Polygon)){if(FVector::Distance(Trial[I].Center,AcceptedPlacements[I].Center)>1)Accepted++;AcceptedPlacements[I]=Trial[I];}}RecordPhase(Result,TEXT("v7-2-guarded"),Before,AcceptedPlacements,Protected.Num(),Accepted);
		}
		for(int32 Stage=0;Stage<3;++Stage)Apply(*FString::Printf(TEXT("v9-%d-final"),Stage+1),[&](const auto&P){return SolveV9Stage(Polygon,P,Stage);});
		Result.bSolved=LayoutValid(Result.Placements,Polygon);for(int32 I=0;I<Source.Num();++I){const double D=FVector::Distance(Source[I].Center,Result.Placements[I].Center);if(D>1)Result.BuildingsMovedCount++;Result.TotalShiftCm+=D;}return Result;
	}
}
